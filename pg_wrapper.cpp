#include "pg_wrapper.h"

namespace pg_wrapper {

DatabaseError::DatabaseError(const std::string& msg)
    : std::runtime_error(msg) {}

ConnectionError::ConnectionError(const std::string& msg)
    : DatabaseError("Connection error: " + msg) {}

QueryError::QueryError(const std::string& msg)
    : DatabaseError("Query error: " + msg) {}

Row::Row(const pqxx::row& row) : row_(row) {}

// Get value by column index
template <typename T>
T Row::get(int col) const {
    if (col >= row_.size()) {
        throw std::out_of_range("Column index out of range");
    }
    return row_[col].as<T>();
}

// Get value by column name
template <typename T>
T Row::get(const std::string& colName) const {
    return row_[colName].as<T>();
}

// Get optional value (returns nullopt if NULL)
template <typename T>
std::optional<T> Row::get_optional(int col) const {
    if (col >= row_.size()) {
        throw std::out_of_range("Column index out of range");
    }
    return row_[col].is_null() ? std::nullopt
                               : std::make_optional(row_[col].as<T>());
}

template <typename T>
std::optional<T> Row::get_optional(const std::string& col_name) const {
    auto field = row_[col_name];
    return field.is_null() ? std::nullopt : std::make_optional(field.as<T>());
}

// Check if column is NULL
bool Row::is_null(int col) const {
    return col < row_.size() && row_[col].is_null();
}

bool Row::is_null(const std::string& colName) const {
    return row_[colName].is_null();
}

// Get number of columns
size_t Row::size() const { return row_.size(); }

// // Column name access
// std::string Row::column_name(size_t col) const {
//     return row_.column_name(col);
// }

Result::iterator::iterator(pqxx::result::const_iterator itr) : itr_(itr) {}

Row Result::iterator::operator*() const { return Row(*itr_); }

bool Result::iterator::operator==(const Result::iterator& itr) const {
    return itr_ == itr.itr_;
}

bool Result::iterator::operator!=(const Result::iterator& itr) const {
    return itr_ != itr.itr_;
}

Result::iterator& Result::iterator::operator++() {
    ++itr_;
    return *this;
}

Result::iterator Result::iterator::operator++(int) {
    auto itr = *this;
    ++itr_;
    return itr;
}

Result::Result(const pqxx::result& result) : result_(result) {}

Result::iterator Result::begin() const {
    return Result::iterator(result_.begin());
}
Result::iterator Result::end() const { return Result::iterator(result_.end()); }

// Access rows
Row Result::operator[](size_t rowNum) const {
    if (rowNum >= result_.size()) {
        throw std::out_of_range("Row index out of range");
    }
    return Row(result_[rowNum]);
}

Row Result::at(size_t rowNum) const { return (*this)[rowNum]; }

// Get first row (throws if empty)
Row Result::front() const {
    if (result_.empty()) {
        throw std::runtime_error("Result is empty");
    }
    return Row(result_.front());
}

// Get first row as optional
std::optional<Row> Result::front_optional() const {
    return result_.empty() ? std::nullopt
                           : std::make_optional(Row(result_.front()));
}

// Result properties
size_t Result::size() const { return result_.size(); }
bool Result::empty() const { return result_.empty(); }
size_t Result::columns() const { return result_.columns(); }
size_t Result::affected_rows() const { return result_.affected_rows(); }

// Column information
std::string Result::column_name(size_t col) const {
    return result_.column_name(col);
}

// Convert all rows to vector
template <typename T>
std::vector<T> Result::to_vector(std::function<T(const Row&)> converter) const {
    std::vector<T> vec;
    vec.reserve(size());
    for (const auto& row : *this) {
        vec.push_back(converter(row));
    }
    return vec;
}

Transaction::Transaction(pqxx::connection& conn)
    : txn_(std::make_unique<pqxx::work>(conn)), committed_(false) {}

Transaction::~Transaction() {
    if (!committed_) {
        try {
            txn_->abort();
        } catch (...) {
            // Ignore exceptions in destructor
        }
    }
}

// Execute query
Result Transaction::exec(const std::string& sql) {
    try {
        return Result(txn_->exec(sql));
    } catch (const pqxx::sql_error& e) {
        throw QueryError(e.what());
    } catch (const std::exception& e) {
        throw DatabaseError(e.what());
    }
}

// Execute parameterized query
template <typename... Args>
Result Transaction::exec_params(const std::string& sql, Args&&... args) {
    try {
        return Result(txn_->exec_params(sql, std::forward<Args>(args)...));
    } catch (const pqxx::sql_error& e) {
        throw QueryError(e.what());
    } catch (const std::exception& e) {
        throw DatabaseError(e.what());
    }
}

// Execute prepared statement
template <typename... Args>
Result Transaction::exec_prepared(const std::string& name, Args&&... args) {
    try {
        return Result(txn_->exec_prepared(name, std::forward<Args>(args)...));
    } catch (const pqxx::sql_error& e) {
        throw QueryError(e.what());
    } catch (const std::exception& e) {
        throw DatabaseError(e.what());
    }
}

// Commit transaction
void Transaction::commit() {
    if (committed_) {
        throw std::runtime_error("Transaction already committed");
    }
    try {
        txn_->commit();
        committed_ = true;
    } catch (const std::exception& e) {
        throw DatabaseError(e.what());
    }
}

// Abort transaction
void Transaction::abort() {
    if (!committed_) {
        txn_->abort();
        committed_ = true;  // Mark as completed to avoid double-abort
    }
}

// Quote and escape values
std::string Transaction::quote(const std::string& value) {
    return txn_->quote(value);
}

std::string Transaction::quote_name(const std::string& name) {
    return txn_->quote_name(name);
}

// Constructor with connection string
Database::Database(const std::string& connectionString) {
    try {
        conn_ = std::make_unique<pqxx::connection>(connectionString);
    } catch (const std::exception& e) {
        throw ConnectionError(e.what());
    }
}

// Constructor with individual parameters
Database::Database(const std::string& host, const std::string& port,
                   const std::string& dbname, const std::string& user,
                   const std::string& password) {
    std::ostringstream oss;
    oss << "host=" << host << " port=" << port << " dbname=" << dbname
        << " user=" << user << " password=" << password;

    try {
        conn_ = std::make_unique<pqxx::connection>(oss.str());
    } catch (const std::exception& e) {
        throw ConnectionError(e.what());
    }
}

// Create transaction
Transaction Database::begin_transaction() {
    if (!is_open()) {
        throw ConnectionError("Connection is not open");
    }
    return Transaction(*conn_);
}

// Execute query without transaction (auto-commit)
Result Database::exec(const std::string& sql) {
    auto txn = begin_transaction();
    auto result = txn.exec(sql);
    txn.commit();
    return result;
}

// Execute parameterized query without transaction
template <typename... Args>
Result Database::exec_params(const std::string& sql, Args&&... args) {
    auto txn = begin_transaction();
    auto result = txn.exec_params(sql, std::forward<Args>(args)...);
    txn.commit();
    return result;
}

// Prepare statement
void Database::prepare(const std::string& name, const std::string& sql) {
    try {
        conn_->prepare(name, sql);
    } catch (const std::exception& e) {
        throw DatabaseError(e.what());
    }
}

// Execute prepared statement without transaction
template <typename... Args>
Result Database::exec_prepared(const std::string& name, Args&&... args) {
    auto txn = begin_transaction();
    auto result = txn.exec_prepared(name, std::forward<Args>(args)...);
    txn.commit();
    return result;
}

// Check if table exists
bool Database::table_exists(const std::string& tableName) {
    auto result = exec_params(
        "SELECT EXISTS (SELECT FROM information_schema.tables WHERE "
        "table_name "
        "= $1)",
        tableName);
    return result.front().get<bool>(0);
}

// Get table column names
std::vector<std::string> Database::get_columns(const std::string& tableName) {
    auto result = exec_params(
        "SELECT column_name FROM information_schema.columns WHERE "
        "table_name = "
        "$1 ORDER BY ordinal_position",
        tableName);

    std::vector<std::string> columns;
    for (const auto& row : result) {
        columns.push_back(row.get<std::string>(0));
    }
    return columns;
}

// Simple insert helper
template <typename... Args>
void Database::insert(const std::string& table,
                      const std::vector<std::string>& columns,
                      Args&&... values) {
    if (sizeof...(values) != columns.size()) {
        throw std::invalid_argument(
            "Number of values doesn't match number of columns");
    }

    std::stringstream sql;
    sql << "INSERT INTO " << table << " (";
    for (size_t i = 0; i < columns.size(); ++i) {
        if (i > 0) sql << ", ";
        sql << columns[i];
    }
    sql << ") VALUES (";
    for (size_t i = 0; i < columns.size(); ++i) {
        if (i > 0) sql << ", ";
        sql << "$" << (i + 1);
    }
    sql << ")";

    exec_params(sql.str(), std::forward<Args>(values)...);
}

// Close connection
void Database::close() {
    if (conn_) {
        conn_.reset();  // conn_->close();
    }
}

ConnectionPool::ConnectionPool(const std::string& connectionString,
                               size_t maxConnections)
    : connectionString_(connectionString), maxConnections_(maxConnections) {
    pool_.reserve(maxConnections_);
}

ConnectionPool::~ConnectionPool() {
    std::lock_guard<std::mutex> lock(mutex_);
    pool_.clear();  // ensures cleanup
}

std::unique_ptr<Database> ConnectionPool::get_connection() {
    std::lock_guard<std::mutex> lock(mutex_);

    // If there's an available one in the pool, return it
    if (!pool_.empty()) {
        auto conn = std::move(pool_.back());
        pool_.pop_back();
        return conn;
    }

    // If we haven't reached the max, create a new one
    if (currentConnections_ < maxConnections_) {
        ++currentConnections_;
        return std::make_unique<Database>(connectionString_);
    }

    // Pool exhausted
    return nullptr;
}

void ConnectionPool::return_connection(std::unique_ptr<Database> conn) {
    if (!conn || !conn->is_open()) {
        // Drop broken connection
        std::lock_guard<std::mutex> lock(mutex_);
        --currentConnections_;
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (pool_.size() < maxConnections_) {
        pool_.push_back(std::move(conn));
    } else {
        // Pool full — destroy the extra connection
        --currentConnections_;
    }
}

}  // namespace pg_wrapper

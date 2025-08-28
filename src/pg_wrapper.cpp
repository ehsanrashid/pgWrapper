#include "pg_wrapper.h"

namespace pg_wrapper {

DatabaseError::DatabaseError(const std::string& msg)
    : std::runtime_error(msg) {}

ConnectionError::ConnectionError(const std::string& msg)
    : DatabaseError("Connection error: " + msg) {}

QueryError::QueryError(const std::string& msg)
    : DatabaseError("Query error: " + msg) {}

Row::Row(const pqxx::row& row) : _row(row) {}

// Get value by column index
template <typename T>
T Row::get(int col) const {
    if (col >= _row.size()) {
        throw std::out_of_range("Column index out of range");
    }
    return _row[col].as<T>();
}

// Get value by column name
template <typename T>
T Row::get(const std::string& colName) const {
    return _row[colName].as<T>();
}

// Get optional value (returns nullopt if NULL)
template <typename T>
std::optional<T> Row::get_optional(int col) const {
    if (col >= _row.size()) {
        throw std::out_of_range("Column index out of range");
    }
    return _row[col].is_null() ? std::nullopt
                               : std::make_optional(_row[col].as<T>());
}

template <typename T>
std::optional<T> Row::get_optional(const std::string& colName) const {
    auto field = _row[colName];
    return field.is_null() ? std::nullopt : std::make_optional(field.as<T>());
}

// Check if column is NULL
bool Row::is_null(int col) const {
    return col < _row.size() && _row[col].is_null();
}

bool Row::is_null(const std::string& colName) const {
    return _row[colName].is_null();
}

// Get number of columns
size_t Row::size() const { return _row.size(); }

// // Column name access
// std::string Row::column_name(size_t col) const {
//     return _row.column_name(col);
// }

Result::iterator::iterator(pqxx::result::const_iterator itr) : _itr(itr) {}

Row Result::iterator::operator*() const { return Row(*_itr); }

bool Result::iterator::operator==(const Result::iterator& itr) const {
    return _itr == itr._itr;
}

bool Result::iterator::operator!=(const Result::iterator& itr) const {
    return _itr != itr._itr;
}

Result::iterator& Result::iterator::operator++() {
    ++_itr;
    return *this;
}

Result::iterator Result::iterator::operator++(int) {
    auto itr = *this;
    ++_itr;
    return itr;
}

Result::Result(const pqxx::result& result) : _result(result) {}

Result::iterator Result::begin() const {
    return Result::iterator(_result.begin());
}
Result::iterator Result::end() const { return Result::iterator(_result.end()); }

// Access rows
Row Result::operator[](size_t rowNum) const {
    if (rowNum >= _result.size()) {
        throw std::out_of_range("Row index out of range");
    }
    return Row(_result[rowNum]);
}

Row Result::at(size_t rowNum) const { return (*this)[rowNum]; }

// Get first row (throws if empty)
Row Result::front() const {
    if (_result.empty()) {
        throw std::runtime_error("Result is empty");
    }
    return Row(_result.front());
}

// Get first row as optional
std::optional<Row> Result::front_optional() const {
    return _result.empty() ? std::nullopt
                           : std::make_optional(Row(_result.front()));
}

// Result properties
size_t Result::size() const { return _result.size(); }
bool Result::empty() const { return _result.empty(); }
size_t Result::columns() const { return _result.columns(); }
size_t Result::affected_rows() const { return _result.affected_rows(); }

// Column information
std::string Result::column_name(size_t col) const {
    return _result.column_name(col);
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
    : _txn(std::make_unique<pqxx::work>(conn)), _committed(false) {}

Transaction::~Transaction() {
    if (!_committed) {
        try {
            _txn->abort();
        } catch (...) {
            // Ignore exceptions in destructor
        }
    }
}

// Execute query
Result Transaction::exec(const std::string& sql) {
    try {
        return Result(_txn->exec(sql));
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
        return Result(_txn->exec_params(sql, std::forward<Args>(args)...));
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
        return Result(_txn->exec_prepared(name, std::forward<Args>(args)...));
    } catch (const pqxx::sql_error& e) {
        throw QueryError(e.what());
    } catch (const std::exception& e) {
        throw DatabaseError(e.what());
    }
}

// Commit transaction
void Transaction::commit() {
    if (_committed) {
        throw std::runtime_error("Transaction already committed");
    }
    try {
        _txn->commit();
        _committed = true;
    } catch (const std::exception& e) {
        throw DatabaseError(e.what());
    }
}

// Abort transaction
void Transaction::abort() {
    if (!_committed) {
        _txn->abort();
        _committed = true;  // Mark as completed to avoid double-abort
    }
}

// Quote and escape values
std::string Transaction::quote(const std::string& value) {
    return _txn->quote(value);
}

std::string Transaction::quote_name(const std::string& name) {
    return _txn->quote_name(name);
}

// Constructor with connection string
Database::Database(const std::string& connectionString) {
    try {
        _conn = std::make_unique<pqxx::connection>(connectionString);
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
        _conn = std::make_unique<pqxx::connection>(oss.str());
    } catch (const std::exception& e) {
        throw ConnectionError(e.what());
    }
}

// Create transaction
Transaction Database::begin_transaction() {
    if (!is_open()) {
        throw ConnectionError("Connection is not open");
    }
    return Transaction(*_conn);
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
        _conn->prepare(name, sql);
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

    std::ostringstream oss;
    oss << "INSERT INTO " << table << " (";
    for (size_t i = 0; i < columns.size(); ++i) {
        if (i > 0) oss << ", ";
        oss << columns[i];
    }
    oss << ") VALUES (";
    for (size_t i = 0; i < columns.size(); ++i) {
        if (i > 0) oss << ", ";
        oss << "$" << (i + 1);
    }
    oss << ")";

    exec_params(oss.str(), std::forward<Args>(values)...);
}

// Close connection
void Database::close() {
    if (_conn) {
        _conn.reset();  // _conn->close();
    }
}

ConnectionPool::ConnectionPool(const std::string& connectionString,
                               size_t maxConnections)
    : _connectionString(connectionString), _maxConnections(maxConnections) {
    _pool.reserve(_maxConnections);
}

ConnectionPool::~ConnectionPool() {
    std::lock_guard lockGuard(_mutex);
    _pool.clear();  // ensures cleanup
}

std::unique_ptr<Database> ConnectionPool::get_connection() {
    std::lock_guard lockGuard(_mutex);

    // If there's an available one in the pool, return it
    if (!_pool.empty()) {
        auto conn = std::move(_pool.back());
        _pool.pop_back();
        return conn;
    }

    // If we haven't reached the max, create a new one
    if (_currentConnections < _maxConnections) {
        ++_currentConnections;
        return std::make_unique<Database>(_connectionString);
    }

    // Pool exhausted
    return nullptr;
}

void ConnectionPool::return_connection(std::unique_ptr<Database> conn) {
    if (!conn || !conn->is_open()) {
        // Drop broken connection
        std::lock_guard lockGuard(_mutex);
        --_currentConnections;
        return;
    }

    std::lock_guard lockGuard(_mutex);
    if (_pool.size() < _maxConnections) {
        _pool.push_back(std::move(conn));
    } else {
        // Pool full — destroy the extra connection
        --_currentConnections;
    }
}

}  // namespace pg_wrapper

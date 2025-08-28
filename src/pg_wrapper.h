#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <pqxx/pqxx>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace pg_wrapper {

// Exception classes
class DatabaseError : public std::runtime_error {
   public:
    explicit DatabaseError(const std::string& msg);
};

class ConnectionError : public DatabaseError {
   public:
    explicit ConnectionError(const std::string& msg);
};

class QueryError : public DatabaseError {
   public:
    explicit QueryError(const std::string& msg);
};

// Forward declarations
class Row;
class Result;
class Transaction;
class Database;

// Row class - represents a single row from query results
class Row {
   public:
    explicit Row(const pqxx::row& row);

    // Get value by column index
    template <typename T>
    T get(int col) const;

    // Get value by column name
    template <typename T>
    T get(const std::string& colName) const;

    // Get optional value (returns nullopt if NULL)
    template <typename T>
    std::optional<T> get_optional(int col) const;

    template <typename T>
    std::optional<T> get_optional(const std::string& colName) const;

    // Check if column is NULL
    bool is_null(int col) const;

    bool is_null(const std::string& colName) const;

    // Get number of columns
    size_t size() const;

    // // Column name access
    // std::string column_name(size_t col) const;

   private:
    pqxx::row _row;
};

// Result class - represents query results
class Result {
   public:
    explicit Result(const pqxx::result& result);

    // Iterator support
    class iterator {
       public:
        explicit iterator(pqxx::result::const_iterator itr);

        Row operator*() const;

        bool operator==(const iterator& itr) const;
        bool operator!=(const iterator& itr) const;

        iterator& operator++();
        iterator operator++(int);

       private:
        pqxx::result::const_iterator _itr;
    };

    iterator begin() const;
    iterator end() const;

    // Access rows
    Row operator[](size_t rowNum) const;

    Row at(size_t rowNum) const;

    // Get first row (throws if empty)
    Row front() const;

    // Get first row as optional
    std::optional<Row> front_optional() const;

    // Result properties
    size_t size() const;
    bool empty() const;
    size_t columns() const;
    size_t affected_rows() const;

    // Column information
    std::string column_name(size_t col) const;

    // Convert all rows to vector
    template <typename T>
    std::vector<T> to_vector(std::function<T(const Row&)> converter) const;

   private:
    pqxx::result _result;
};

// Transaction class
class Transaction {
   public:
    explicit Transaction(pqxx::connection& conn);

    ~Transaction();

    // Execute query
    Result exec(const std::string& sql);

    // Execute parameterized query
    template <typename... Args>
    Result exec_params(const std::string& sql, Args&&... args);

    // Execute prepared statement
    template <typename... Args>
    Result exec_prepared(const std::string& name, Args&&... args);

    // Commit transaction
    void commit();

    // Abort transaction
    void abort();

    // Quote and escape values
    std::string quote(const std::string& value);

    std::string quote_name(const std::string& name);

   private:
    std::unique_ptr<pqxx::work> _txn;
    bool _committed;
};

// Database connection class
class Database {
   public:
    // Constructor with connection string
    explicit Database(const std::string& connectionString);

    // Constructor with individual parameters
    Database(const std::string& host, const std::string& port,
             const std::string& dbname, const std::string& user,
             const std::string& password);

    // Check if connected
    bool is_open() const { return _conn && _conn->is_open(); }

    // Get connection info
    std::string dbname() const { return _conn->dbname(); }
    std::string username() const { return _conn->username(); }
    std::string hostname() const { return _conn->hostname(); }
    std::string port() const { return _conn->port(); }

    // Create transaction
    Transaction begin_transaction();

    // Execute query without transaction (auto-commit)
    Result exec(const std::string& sql);

    // Execute parameterized query without transaction
    template <typename... Args>
    Result exec_params(const std::string& sql, Args&&... args);

    // Prepare statement
    void prepare(const std::string& name, const std::string& sql);

    // Execute prepared statement without transaction
    template <typename... Args>
    Result exec_prepared(const std::string& name, Args&&... args);

    // Utility methods for common operations

    // Check if table exists
    bool table_exists(const std::string& tableName);

    // Get table column names
    std::vector<std::string> get_columns(const std::string& tableName);

    // Simple insert helper
    template <typename... Args>
    void insert(const std::string& table,
                const std::vector<std::string>& columns, Args&&... values);

    // Close connection
    void close();

   private:
    std::unique_ptr<pqxx::connection> _conn;
};

// Connection pool class for multi-threaded applications
class ConnectionPool {
   public:
    explicit ConnectionPool(const std::string& connectionString,
                            size_t maxConnections = 10);

    ~ConnectionPool();

    std::unique_ptr<Database> get_connection();

    void return_connection(std::unique_ptr<Database> conn);

   private:
    std::string _connectionString;
    std::vector<std::unique_ptr<Database>> _pool;
    std::mutex _mutex;
    size_t _maxConnections;
    size_t _currentConnections{0};  // Tracks live connections
};

}  // namespace pg_wrapper
# pgWrapper

A modern, C++17-friendly wrapper around [libpqxx](https://github.com/jtv/libpqxx) for PostgreSQL, providing a safer, more convenient, and idiomatic interface for database access, query execution, and result handling. Includes support for connection pooling and advanced NULL handling using `std::optional`.

## Features

- Simple, RAII-based database connection management
- Easy transaction handling
- Parameterized queries and prepared statements
- Type-safe row and result access
- Idiomatic NULL handling with `std::optional`
- Connection pooling for multi-threaded applications
- Utility methods for common database operations
- Exception-based error handling

## Requirements

- C++17 or newer
- [libpqxx](https://github.com/jtv/libpqxx) (and its dependencies)
- PostgreSQL client libraries
- CMake 3.16+

## Building

```bash
git clone https://github.com/ehsanrashid/pgWrapper
cd pgWrapper
mkdir build && cd build
cmake ..
make -j$(nproc)
sudo make install #to install lib and headers locally
```

This will build the `pgWrapper` library and install it to your system. The headers will be installed to `/usr/local/include/` and the library to `/usr/local/lib/`. You can then link it to your own projects or use the provided example in `main.cpp_`.

## Usage Example

Below is a minimal example of how to use `pgWrapper` to connect to a PostgreSQL database, insert and query data, and handle NULL values:

```cpp
#include "pg_wrapper.h"
#include <iostream>
#include <optional>

int main() {
    try {
        pg_wrapper::Database db("<host>>", "<port#>", "<database_name>", "<user>", "<password>");

        // Create a table
        db.exec("CREATE TABLE IF NOT EXISTS test (id SERIAL PRIMARY KEY, name TEXT, age INTEGER)");

        // Insert with NULL using std::optional
        std::optional<int> null_age;
        db.exec_params("INSERT INTO test (name, age) VALUES ($1, $2)", "Alice", null_age);

        // Query and handle NULLs
        auto result = db.exec("SELECT id, name, age FROM test");
        for (const auto& row : result) {
            std::cout << "Name: " << row.get<std::string>("name");
            if (auto age = row.get_optional<int>("age")) {
                std::cout << ", Age: " << *age;
            } else {
                std::cout << ", Age: NULL";
            }
            std::cout << std::endl;
        }

        db.close();
    } catch (const pg_wrapper::DatabaseError& e) {
        std::cerr << "Database error: " << e.what() << std::endl;
    }
}
```

See `main.cpp_` for a comprehensive demonstration of advanced NULL handling and more.

## API Overview

- **pg_wrapper::Database**: Manages a PostgreSQL connection. Supports direct queries, parameterized queries, prepared statements, and utility methods.
- **pg_wrapper::Transaction**: RAII transaction object for executing multiple queries atomically.
- **pg_wrapper::Result**: Represents query results, supports iteration and type-safe access.
- **pg_wrapper::Row**: Represents a single row, provides type-safe getters and NULL checks.
- **pg_wrapper::ConnectionPool**: Simple connection pool for multi-threaded use.

### Exception Hierarchy

- `pg_wrapper::DatabaseError`
  - `pg_wrapper::ConnectionError`
  - `pg_wrapper::QueryError`


## Author

- Ehsan Rashid
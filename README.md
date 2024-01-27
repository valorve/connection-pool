# Connection pool for pqxx (C++ PostgreSQL API)

## Get started
The pqxx connection pool library is a single-header C++ library designed for managing a pool of connections to a PostgreSQL database using the libpqxx library. This library provides an efficient way to handle multiple database connections in a thread-safe manner.

## Features

- Connection Pooling & Automatic Connection Management
- Thread-Safety

## Requirements

- `C++20` or newer
    - or you can use `C++17` and replace `std::format` with `fmt::format` using [fmt](https://github.com/fmtlib/)
- `libpqxx` installed and configured in your development environment
- PostgreSQL server accessible on your machine

## Installation

Include the `pqxx_connection_pool.hpp` header in your project

## Usage

### Setting Up Connection Options

Define the connection parameters using the connection_options struct:

```c++
cp::connection_options options;
options.dbname = "your_database";
options.user = "your_username";
options.password = "your_password";
options.hostaddr = "database_host_address";
options.port = 5432; // Default PostgreSQL port
options.connections_count = 8; // Default number of connections in the pool
```

### Creating a Connection Pool

Instantiate a `connection_pool` object with the defined options:

```c++
cp::connection_pool pool(options);
```

### Using Transactions

To perform database operations, create a transaction:

```c++
{
    auto tx = cp::tx(pool);
    // Execute your queries
    tx.commit();
}
// Connection is automatically returned to the pool
```

### Unnamed Queries

```c++
cp::query my_query("SELECT * FROM my_table WHERE id = $1");
auto tx = cp::tx(pool, my_query);
pqxx::result result = my_query(1); // Execute the query with parameter
tx.commit();
```

### Named Queries
But it is preferable to use named queries, because in case of an error, the Posgres driver responds with an error indicating in which query it occurred.

```c++
cp::named_query my_query("my_query", "SELECT * FROM my_table WHERE id = $1");
auto tx = cp::tx(pool, my_query);
pqxx::result result = my_query(1); // Execute the query with parameter
tx.commit();
```

### Get access to `pqxx::work` from Transaction

```c++
auto tx = cp::tx(pool, ....);
auto& work = tx.get();

// Execute queries

work.commit();
```
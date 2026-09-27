# System Architecture & Detailed Job Catalog

This document details the architectural design, control flows, and a complete catalog of every job performed by **nasr-pg**.

---

## 1. High-Level Architecture

`nasr-pg` acts as an adapter library between the generic Alfi database interfaces (`alfi::db::Driver`, `alfi::db::Connection`) and the underlying PostgreSQL C client library (`libpq`).

```mermaid
flowchart TD
    App["Alfi Application"] --> Registry["alfi::db::DriverRegistry"]
    Registry --> Driver["nasrpg::PgDriver"]
    Driver -->|connect(dsn)| Conn["nasrpg::PgConnection"]
    Conn -->|owns| PGconnHandle["libpq PGconn*"]
    
    subgraph "libpq C API"
        PGconnHandle --> PQconnectdb["PQconnectdb()"]
        PGconnHandle --> PQexecParams["PQexecParams()"]
        PGconnHandle --> PQexec["PQexec()"]
        PGconnHandle --> PQfinish["PQfinish()"]
    end

    subgraph "PostgreSQL Server"
        PQconnectdb <--> PostgresDB[("PostgreSQL Database")]
        PQexecParams <--> PostgresDB
        PQexec <--> PostgresDB
    end
```

---

## 2. Complete Job Catalog

Below is an exhaustive, itemized list of **every single job** performed by `nasr-pg` across its lifecycle:

### Job 1: Driver Registration & Identification
- **Identifier**: `PgDriver::name()`
- **Responsibilities**:
  - Implements `alfi::db::Driver::name()`.
  - Returns `"postgres"` to allow registration in `alfi::db::DriverRegistry`.

### Job 2: PostgreSQL Connection Establishment
- **Identifier**: `PgDriver::connect(const std::string& dsn)`
- **Responsibilities**:
  - Receives DSN strings in URI format (`postgres://...`) or key-value conninfo format (`host=... dbname=...`).
  - Calls libpq's `PQconnectdb(dsn.c_str())` to allocate and initialize a `PGconn` instance.
  - Verifies allocation (checks if pointer is non-null).
  - Evaluates `PQstatus(conn) == CONNECTION_OK`.
  - On failure: retrieves error diagnostic via `PQerrorMessage(conn)`, cleans up memory using `PQfinish(conn)`, and throws `alfi::db::DbError` with code `"CONNECTION_FAILURE"`.
  - On success: wraps `PGconn*` into a `std::unique_ptr<nasrpg::PgConnection>`.

### Job 3: Resource Ownership & RAII Connection Lifetime Management
- **Identifier**: `PgConnection` Constructor / Destructor / Move Operations
- **Responsibilities**:
  - Stores private raw `PGconn* conn_`.
  - Enforces move-only semantics (`delete` copy constructor & copy assignment).
  - Ensures clean shutdown by invoking `PQfinish(conn_)` in the destructor when `conn_` is non-null.
  - Handles move constructor and move assignment safely by nullifying the source connection handle.

### Job 4: Parameter Serialization & Marshalling
- **Identifier**: `paramToString()` static helper
- **Responsibilities**:
  - Inspects `alfi::db::Param` variant types (`std::monostate`, `bool`, `int64_t`, `double`, `std::string`).
  - Handles NULL parameters by populating `isNull = true` and setting target pointer to `nullptr`.
  - Converts non-null variants into C-style text strings (`"t"`/`"f"` for booleans, `std::to_string` for numbers, raw strings for text).
  - Generates parallel arrays (`paramStrings`, `paramValues`) required by `PQexecParams`.

### Job 5: Parameterized Query Execution (SQL Injection Prevention)
- **Identifier**: `PgConnection::execute(sql, params)`
- **Responsibilities**:
  - Checks if `conn_` is open; throws `DbError` ("CONNECTION_CLOSED") if null.
  - Marshals parameters via Job 4.
  - Calls `PQexecParams()` with text format (`resultFormat = 0`) and server-side parameter type inference.
  - Evaluates returned `PGresult*` status using `PQresultStatus()`.

### Job 6: Non-SELECT Command Processing (Mutation Tracking)
- **Identifier**: `PgConnection::execute()` - `PGRES_COMMAND_OK` branch
- **Responsibilities**:
  - Invokes `PQcmdTuples(res)` to extract affected row strings for `INSERT`, `UPDATE`, `DELETE`, etc.
  - Converts string counts to `std::size_t` stored in `QueryResult::affectedRows`.
  - Clears libpq memory via `PQclear(res)` before returning.

### Job 7: SELECT Tuple Processing & Type Parsing
- **Identifier**: `PgConnection::execute()` - `PGRES_TUPLES_OK` branch
- **Responsibilities**:
  - Obtains column count (`PQnfields`) and row count (`PQntuples`).
  - Extracts column header names using `PQfname()`.
  - Iterates over tuples and cells.
  - Checks cell nullability using `PQgetisnull()`.
  - Fetches cell column OID via `PQftype()`.
  - Converts cell text representation (`PQgetvalue()`) into appropriate `alfi::db::Value` variant using Job 8.
  - Populates `QueryResult::rows` and sets `affectedRows` equal to row count.
  - Clears `PGresult*` handle via `PQclear(res)`.

### Job 8: OID to Value Type Mapping & Parsing
- **Identifier**: `pgValueFromText(Oid oid, const char* text)`
- **Responsibilities**:
  - Maps PostgreSQL OID catalog types (`BOOLOID`, `INT8OID`, `INT4OID`, `INT2OID`, `FLOAT4OID`, `FLOAT8OID`, `NUMERICOID`, `TEXTOID`, `VARCHAROID`, `BPCHAROID`, `NAMEOID`).
  - Converts text strings to numeric/boolean types via `std::strtoll` and `std::strtod`.
  - Provides a safe, lossless fallback: unmapped OIDs return the unparsed `std::string` text representation.

### Job 9: Transaction Management (BEGIN, COMMIT, ROLLBACK)
- **Identifier**: `beginTransaction()`, `commit()`, `rollback()`, and helper `executeSimple()`
- **Responsibilities**:
  - Executes explicit SQL statements (`BEGIN`, `COMMIT`, `ROLLBACK`) via `PQexec()`.
  - Checks command execution status (`PGRES_COMMAND_OK`).
  - Throws `DbError` with SQLSTATE code on transaction failure.

### Job 10: Error Diagnostics & Exception Translation
- **Identifier**: Failure handlers across `pg_driver.cpp` and `pg_connection.cpp`
- **Responsibilities**:
  - Extracts detailed error strings using `PQerrorMessage()` or `PQresultErrorMessage()`.
  - Extracts 5-character PostgreSQL SQLSTATE codes via `PQresultErrorField(res, PG_DIAG_SQLSTATE)`.
  - Constructs and throws unified `alfi::db::DbError` exceptions with exact SQLSTATE diagnostic codes.

### Job 11: Dependency Management & Build Automation
- **Identifier**: `CMakeLists.txt` & `cmake/FetchDependencies.cmake`
- **Responsibilities**:
  - Configures C++17 build targets.
  - Downloads and extracts Alfi Core headers into an interface library target (`alfi_db_interface`).
  - Discovers system-installed `libpq` using `pkg-config`.
  - Conditionally fetches `Catch2` v3.4.0 when `NASRPG_BUILD_TESTS=ON`.

### Job 12: Automated Integration Testing
- **Identifier**: `tests/test_pg_connection.cpp`
- **Responsibilities**:
  - Reads `NASRPG_TEST_DSN` environment variable.
  - Skips tests cleanly if database configuration is missing.
  - Exercises connection establishment, invalid DSN handling, parameterized query execution, NULL value handling, syntax error handling, transaction persistence and rollback verification, type mapping checks, and `DriverRegistry` integration.

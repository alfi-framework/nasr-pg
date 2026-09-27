# Error Handling & Exception Management Specification

This document details all error handling strategies, error classifications, exception guarantees, and diagnostic code mappings in **nasr-pg**.

---

## 1. Error Handling Philosophy

`nasr-pg` employs a strict **Exception-Based Error Model**. 

- All database, network, memory, syntax, and transaction errors throw **`alfi::db::DbError`** exceptions (defined in Alfi core).
- No silent failures, fallback dummy data, or swallowed exceptions occur anywhere in `nasr-pg`.
- Memory allocations (e.g. `PGconn*`, `PGresult*`) are guaranteed to be cleaned up before any exception is thrown, ensuring **Strong Exception Safety**.

---

## 2. Comprehensive Inventory of Error Scenarios & Handling

Below is an itemized breakdown of every error condition detected and handled by `nasr-pg`:

### 2.1 Connection Out-of-Memory Failure
- **Trigger**: `PQconnectdb(dsn)` returns a `nullptr`.
- **Detection**: `if (!conn)` in `PgDriver::connect()`.
- **Action**: Immediately throws `alfi::db::DbError`.
- **Error Message**: `"PQconnectdb returned null — out of memory"`
- **SQLSTATE / Code**: `"CONNECTION_FAILURE"`

### 2.2 Connection Establishment Failure (Bad DSN / Host Unreachable / Auth Failure)
- **Trigger**: `PQconnectdb` returns a valid pointer, but `PQstatus(conn) != CONNECTION_OK`.
- **Detection**: `if (PQstatus(conn) != CONNECTION_OK)` in `PgDriver::connect()`.
- **Action**:
  1. Reads detailed error message via `PQerrorMessage(conn)`.
  2. Cleans up connection handle via `PQfinish(conn)`.
  3. Throws `alfi::db::DbError`.
- **Error Message**: `"Failed to connect to PostgreSQL: <libpq_error_message>"`
- **SQLSTATE / Code**: `"CONNECTION_FAILURE"`

### 2.3 Query Execution on Closed or Moved Connection
- **Trigger**: Caller attempts to invoke `execute()`, `beginTransaction()`, `commit()`, or `rollback()` on a `PgConnection` whose underlying `conn_` pointer is `nullptr` (e.g. after being moved).
- **Detection**: `if (!conn_)` at start of `execute()` and `executeSimple()`.
- **Action**: Throws `alfi::db::DbError`.
- **Error Message**: `"Connection is closed"`
- **SQLSTATE / Code**: `"CONNECTION_CLOSED"`

### 2.4 Query Dispatch Null Result
- **Trigger**: `PQexecParams()` or `PQexec()` returns a `nullptr` handle (typically due to severe connection failure or client-side out-of-memory).
- **Detection**: `if (!res)` after query execution.
- **Action**: Reads diagnostic message via `PQerrorMessage(conn_)` and throws `alfi::db::DbError`.
- **Error Message**: `"PQexecParams returned null: <libpq_error_message>"` or `"PQexec returned null: <libpq_error_message>"`
- **SQLSTATE / Code**: `"QUERY_FAILURE"`

### 2.5 Query Execution & SQL Error (Syntax Error, Constraint Violation, Missing Table, etc.)
- **Trigger**: PostgreSQL server returns an error status (`PGRES_FATAL_ERROR`, `PGRES_BAD_RESPONSE`, etc.).
- **Detection**: `default` branch in `switch (PQresultStatus(res))` inside `execute()`.
- **Action**:
  1. Extracts detailed message via `PQresultErrorMessage(res)`.
  2. Extracts exact 5-character PostgreSQL **SQLSTATE** code via `PQresultErrorField(res, PG_DIAG_SQLSTATE)`.
  3. Frees result handle via `PQclear(res)`.
  4. Throws `alfi::db::DbError`.
- **Error Message**: `"Query failed: <pg_result_error_message>"`
- **SQLSTATE / Code**: Exact PostgreSQL SQLSTATE string (e.g., `"42601"` for syntax error, `"23505"` for unique violation, `"42P01"` for undefined table). Defaults to `"QUERY_FAILURE"` if SQLSTATE is unavailable.

### 2.6 Transaction Command Failures (BEGIN, COMMIT, ROLLBACK)
- **Trigger**: SQL execution status of `BEGIN`, `COMMIT`, or `ROLLBACK` is not `PGRES_COMMAND_OK`.
- **Detection**: `if (status != PGRES_COMMAND_OK)` inside `PgConnection::executeSimple()`.
- **Action**:
  1. Extracts message via `PQresultErrorMessage(res)`.
  2. Extracts SQLSTATE via `PQresultErrorField(res, PG_DIAG_SQLSTATE)`.
  3. Frees result handle via `PQclear(res)`.
  4. Throws `alfi::db::DbError`.
- **Error Message**: `"Transaction command failed: <pg_result_error_message>"`
- **SQLSTATE / Code**: Exact PostgreSQL SQLSTATE code or `"TRANSACTION_FAILURE"`.

---

## 3. SQLSTATE Code Extraction

PostgreSQL uses 5-character alphanumeric SQLSTATE codes (defined in the ANSI SQL standard and PostgreSQL documentation) to classify errors uniquely.

`nasr-pg` extracts this code directly from the result diagnostics:

```cpp
const char* sqlstate = PQresultErrorField(res, PG_DIAG_SQLSTATE);
std::string code = sqlstate ? sqlstate : "QUERY_FAILURE";
```

### Common PostgreSQL SQLSTATE Codes Returned

| SQLSTATE Code | Description | Example Cause |
|---|---|---|
| `42601` | Syntax Error | Misspelled keyword (`SELEKT`) |
| `42P01` | Undefined Table | Table does not exist |
| `42703` | Undefined Column | Column name misspelled |
| `23505` | Unique Violation | Duplicate key inserted into UNIQUE column |
| `23503` | Foreign Key Violation | Referencing non-existent parent row |
| `28P01` | Invalid Password | Bad credentials during connection |
| `3D000` | Invalid Database Name | Database specified in DSN does not exist |

---

## 4. Exception Safety & Resource Cleanup Guarantees

`nasr-pg` guarantees that resources are never leaked when an exception is thrown:

1. **`PGresult*` Cleanup**: Every code path that creates a `PGresult*` handle calls `PQclear(res)` before throwing an exception.
2. **`PGconn*` Cleanup**: If connection establishment fails midway in `PgDriver::connect()`, `PQfinish(conn)` is called before throwing `DbError`.
3. **RAII Destructor**: `PgConnection`'s destructor automatically frees `conn_` when out of scope.

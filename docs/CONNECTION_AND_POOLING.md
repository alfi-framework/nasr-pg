# PostgreSQL Connection Management & Connection Pooling Analysis

This document explains in detail how PostgreSQL connections are established, managed, and used inside **nasr-pg**, and provides a clear analysis of connection pooling.

---

## 1. How PostgreSQL Connection Works in nasr-pg

`nasr-pg` relies directly on **libpq** (PostgreSQL's standard C client library) for establishing and managing database connections.

```mermaid
sequenceDiagram
    participant App as Application / Alfi
    participant Driver as PgDriver
    participant libpq as libpq C API
    participant PGServer as PostgreSQL Server

    App->>Driver: connect("postgres://user:pass@host:5432/db")
    Driver->>libpq: PQconnectdb(dsn)
    libpq->>PGServer: TCP Handshake + TLS + Authentication
    PGServer-->>libpq: Connection OK / Auth Success
    libpq-->>Driver: Returns PGconn* handle
    Driver->>App: Returns std::unique_ptr<PgConnection>
```

### 1.1 DSN Parsing & Connection Strings
When `PgDriver::connect(const std::string& dsn)` is called, the DSN string is passed directly to libpq's `PQconnectdb()`.

`nasr-pg` does **not** parse or validate connection string formats manually. This means `nasr-pg` out-of-the-box supports all connection formats recognized by libpq:

1. **URI Format**:
   ```text
   postgres://username:password@hostname:5432/dbname?sslmode=require
   ```
2. **Key-Value Conninfo Format**:
   ```text
   host=localhost port=5432 dbname=mydb user=myuser password=mypass sslmode=disable connect_timeout=10
   ```

### 1.2 Connection Lifecycle & State Verification
1. **Invocation**: `PgDriver::connect()` calls `PQconnectdb(dsn.c_str())`.
2. **Memory Check**: Checks if `conn == nullptr`. If true, memory allocation failed; throws `DbError` ("CONNECTION_FAILURE").
3. **Status Check**: Calls `PQstatus(conn)`. If the status is NOT `CONNECTION_OK`:
   - Extracts human-readable error text via `PQerrorMessage(conn)`.
   - Calls `PQfinish(conn)` to free allocated resources.
   - Throws `DbError("Failed to connect to PostgreSQL: " + errorMsg, "CONNECTION_FAILURE")`.
4. **Ownership Transfer**: If successful, transfers `PGconn*` to a newly constructed `PgConnection` object wrapped in `std::unique_ptr<alfi::db::Connection>`.

### 1.3 RAII Resource Management
`PgConnection` owns the underlying `PGconn*` pointer:
- **Destructor**: When `PgConnection` goes out of scope, `~PgConnection()` calls `PQfinish(conn_)`.
- **Move Semantics**: `PgConnection` is **non-copyable** (`delete` copy constructor & copy assignment) and **move-only**. Moving a connection transfers ownership of `PGconn*` and sets the source pointer to `nullptr`.

---

## 2. Connection Pooling Analysis

### Is Connection Pooling Done Here?
> [!IMPORTANT]
> **NO.** Connection pooling is **NOT** performed inside `nasr-pg`.

### Why Connection Pooling is Not Implemented in nasr-pg
1. **Interface Scope**: `nasr-pg` implements Alfi core's `alfi::db::Driver` interface. The `Driver::connect()` contract returns a single, independent `std::unique_ptr<alfi::db::Connection>` instance per call.
2. **Single Responsibility**: `nasr-pg` is designed to be a direct, unopinionated low-level driver for PostgreSQL. Adding internal pooling inside `connect()` would violate the driver interface design contract.

### Consequences of 1-Connection-Per-Call
- Every call to `PgDriver::connect()` initiates a new physical TCP connection and PostgreSQL backend process.
- Destroying the `PgConnection` object terminates the physical connection (`PQfinish`).

### How Connection Pooling Should Be Handled

For high-concurrency production workloads using `nasr-pg`, connection pooling can be achieved in two ways:

#### Option A: External Connection Proxy (Recommended for Production)
Use an external connection pooler like **PgBouncer** or **pgpool-II**.
- Configure PgBouncer in front of your PostgreSQL database.
- Pass the PgBouncer endpoint as the DSN to `PgDriver::connect()`:
  ```cpp
  auto conn = driver.connect("postgres://user:pass@127.0.0.1:6432/mydb");
  ```
- **Benefits**: No code changes needed; zero application-level pooling overhead; transparent transaction-level or session-level pooling managed by PgBouncer.

#### Option B: Application-Level Pool Wrapper
In the future, a generic connection pool class (e.g. `alfi::db::ConnectionPool`) can be implemented in the Alfi framework layer. The pool manager would maintain a queue of `std::unique_ptr<nasrpg::PgConnection>` instances created by `PgDriver::connect()`.

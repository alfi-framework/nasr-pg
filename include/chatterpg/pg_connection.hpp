#pragma once

#include <memory>
#include <string>
#include <vector>

#include <alfi/db/connection.hpp>
#include <alfi/db/param.hpp>
#include <alfi/db/query_result.hpp>

// Forward-declare libpq's opaque connection handle so we don't leak
// <libpq-fe.h> into every translation unit that includes this header.
struct pg_conn;
typedef struct pg_conn PGconn;

namespace chatterpg {

/// PgConnection is the Alfi database Connection implementation for PostgreSQL.
///
/// Owns a PGconn* (RAII-managed — PQfinish in destructor). Non-copyable,
/// move-only.
///
/// All queries go through PQexecParams (parameterized) — user-supplied values
/// are NEVER interpolated into the SQL string.
class PgConnection : public alfi::db::Connection {
public:
    /// Constructs a PgConnection that takes ownership of the given PGconn*.
    /// @param conn  A connected PGconn* (caller must NOT call PQfinish).
    explicit PgConnection(PGconn* conn);

    /// Destroys the connection, calling PQfinish on the underlying PGconn*.
    ~PgConnection() override;

    // Non-copyable.
    PgConnection(const PgConnection&) = delete;
    PgConnection& operator=(const PgConnection&) = delete;

    // Move-constructible / move-assignable.
    PgConnection(PgConnection&& other) noexcept;
    PgConnection& operator=(PgConnection&& other) noexcept;

    /// Executes a parameterized SQL query.
    ///
    /// Uses PQexecParams — parameters are bound positionally ($1, $2, …).
    /// For SELECT queries, returns rows + columns.
    /// For INSERT/UPDATE/DELETE, returns affectedRows.
    ///
    /// @throws alfi::db::DbError on query failure or type-mapping error.
    alfi::db::QueryResult execute(
        const std::string& sql,
        const std::vector<alfi::db::Param>& params = {}) override;

    /// Begins a transaction (executes BEGIN).
    /// @throws alfi::db::DbError on failure.
    void beginTransaction() override;

    /// Commits the current transaction (executes COMMIT).
    /// @throws alfi::db::DbError on failure.
    void commit() override;

    /// Rolls back the current transaction (executes ROLLBACK).
    /// @throws alfi::db::DbError on failure.
    void rollback() override;

private:
    PGconn* conn_;

    /// Executes a simple (non-parameterized) SQL command internally.
    /// Used for transaction control (BEGIN / COMMIT / ROLLBACK).
    /// @throws alfi::db::DbError on failure.
    void executeSimple(const std::string& sql);
};

} // namespace chatterpg

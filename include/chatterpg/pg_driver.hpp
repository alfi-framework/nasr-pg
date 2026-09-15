#pragma once

#include <memory>
#include <string>

#include <alfi/db/driver.hpp>

namespace chatterpg {

/// PgDriver is the Alfi database Driver implementation for PostgreSQL.
///
/// Registers under the name "postgres" in alfi::db::DriverRegistry.
/// Uses libpq's PQconnectdb internally, so the DSN accepts any format
/// that libpq supports (postgres:// URIs or key=value conninfo strings).
class PgDriver : public alfi::db::Driver {
public:
    /// Returns "postgres".
    std::string name() const override;

    /// Opens a connection to the given PostgreSQL DSN.
    ///
    /// @param dsn  A libpq-compatible connection string, e.g.
    ///             "postgres://user:pass@localhost:5432/dbname"
    /// @return     A connected PgConnection (as unique_ptr<Connection>).
    /// @throws alfi::db::DbError on connection failure, with the underlying
    ///         libpq error message.
    std::unique_ptr<alfi::db::Connection> connect(const std::string& dsn) override;
};

} // namespace chatterpg

#include "nasrpg/pg_driver.hpp"
#include "nasrpg/pg_connection.hpp"

#include <libpq-fe.h>

#include <alfi/db/db_error.hpp>

namespace nasrpg {

std::string PgDriver::name() const {
    return "postgres";
}

std::unique_ptr<alfi::db::Connection> PgDriver::connect(const std::string& dsn) {
    // PQconnectdb accepts both postgres:// URIs and key=value conninfo strings.
    PGconn* conn = PQconnectdb(dsn.c_str());

    if (!conn) {
        throw alfi::db::DbError(
            "PQconnectdb returned null — out of memory",
            "CONNECTION_FAILURE");
    }

    if (PQstatus(conn) != CONNECTION_OK) {
        std::string errorMsg = PQerrorMessage(conn);
        PQfinish(conn);
        throw alfi::db::DbError(
            "Failed to connect to PostgreSQL: " + errorMsg,
            "CONNECTION_FAILURE");
    }

    return std::make_unique<PgConnection>(conn);
}

} // namespace nasrpg

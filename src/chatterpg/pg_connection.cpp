#include "chatterpg/pg_connection.hpp"

#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include <libpq-fe.h>

#include <alfi/db/db_error.hpp>
#include <alfi/db/value.hpp>

namespace chatterpg {

// ---- Construction / Destruction / Move ------------------------------------

PgConnection::PgConnection(PGconn* conn)
    : conn_(conn) {}

PgConnection::~PgConnection() {
    if (conn_) {
        PQfinish(conn_);
        conn_ = nullptr;
    }
}

PgConnection::PgConnection(PgConnection&& other) noexcept
    : conn_(other.conn_) {
    other.conn_ = nullptr;
}

PgConnection& PgConnection::operator=(PgConnection&& other) noexcept {
    if (this != &other) {
        if (conn_) {
            PQfinish(conn_);
        }
        conn_ = other.conn_;
        other.conn_ = nullptr;
    }
    return *this;
}

// ---- Helpers --------------------------------------------------------------

/// Converts a Param variant into a string representation suitable for
/// PQexecParams, and records whether the param is null.
/// Returns the string value; sets `isNull` to true if the param is null.
static std::string paramToString(const alfi::db::Param& p, bool& isNull) {
    const auto& v = p.variant();

    if (p.isNull()) {
        isNull = true;
        return {};
    }
    isNull = false;

    return std::visit([](auto&& arg) -> std::string {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, std::monostate>) {
            return {};  // unreachable — handled above
        } else if constexpr (std::is_same_v<T, bool>) {
            return arg ? "t" : "f";
        } else if constexpr (std::is_same_v<T, int64_t>) {
            return std::to_string(arg);
        } else if constexpr (std::is_same_v<T, double>) {
            return std::to_string(arg);
        } else if constexpr (std::is_same_v<T, std::string>) {
            return arg;
        } else {
            return {};
        }
    }, v);
}

/// Maps a Postgres OID to an alfi::db::Value by parsing the text-format
/// cell value from PGresult.
///
/// OID reference (from PostgreSQL's pg_type catalog):
///   16   = bool
///   20   = int8 (bigint)
///   21   = int2 (smallint)
///   23   = int4 (integer)
///   700  = float4 (real)
///   701  = float8 (double precision)
///   25   = text
///   1042 = bpchar (char(n))
///   1043 = varchar
///   1700 = numeric
///
/// Unsupported OIDs are returned as string values (lossless fallback).
static alfi::db::Value pgValueFromText(Oid oid, const char* text) {
    switch (oid) {
        // Boolean
        case 16:  // BOOLOID
            return alfi::db::Value(text[0] == 't' || text[0] == 'T');

        // Integer types → int64_t
        case 20:  // INT8OID (bigint)
        case 21:  // INT2OID (smallint)
        case 23:  // INT4OID (integer)
        case 26:  // OIDOID
            return alfi::db::Value(static_cast<int64_t>(std::strtoll(text, nullptr, 10)));

        // Floating-point types → double
        case 700:  // FLOAT4OID (real)
        case 701:  // FLOAT8OID (double precision)
        case 1700: // NUMERICOID (numeric/decimal — lossy, but best effort)
            return alfi::db::Value(std::strtod(text, nullptr));

        // Text / character types → string
        case 25:   // TEXTOID
        case 1042: // BPCHAROID (char(n))
        case 1043: // VARCHAROID
        case 19:   // NAMEOID
        default:
            // All other types: return as string (lossless fallback).
            return alfi::db::Value(std::string(text));
    }
}

// ---- Execute --------------------------------------------------------------

alfi::db::QueryResult PgConnection::execute(
    const std::string& sql,
    const std::vector<alfi::db::Param>& params)
{
    if (!conn_) {
        throw alfi::db::DbError("Connection is closed", "CONNECTION_CLOSED");
    }

    const int nParams = static_cast<int>(params.size());

    // Convert params to C strings for PQexecParams.
    std::vector<std::string> paramStrings(nParams);
    std::vector<const char*> paramValues(nParams);

    for (int i = 0; i < nParams; ++i) {
        bool isNull = false;
        paramStrings[i] = paramToString(params[i], isNull);
        paramValues[i] = isNull ? nullptr : paramStrings[i].c_str();
    }

    // Execute parameterized query.
    // We pass nullptr for paramTypes/paramLengths/paramFormats to let
    // PostgreSQL infer types from context and use text format.
    PGresult* res = PQexecParams(
        conn_,
        sql.c_str(),
        nParams,
        nullptr,       // paramTypes — let server infer
        nParams > 0 ? paramValues.data() : nullptr,
        nullptr,       // paramLengths — not needed for text format
        nullptr,       // paramFormats — all text (0)
        0              // resultFormat — text
    );

    if (!res) {
        throw alfi::db::DbError(
            "PQexecParams returned null: " + std::string(PQerrorMessage(conn_)),
            "QUERY_FAILURE");
    }

    ExecStatusType status = PQresultStatus(res);
    alfi::db::QueryResult result;

    switch (status) {
        case PGRES_COMMAND_OK: {
            // Non-SELECT command (INSERT, UPDATE, DELETE, CREATE, etc.)
            const char* affected = PQcmdTuples(res);
            if (affected && affected[0] != '\0') {
                result.affectedRows = static_cast<std::size_t>(
                    std::strtoull(affected, nullptr, 10));
            }
            PQclear(res);
            return result;
        }

        case PGRES_TUPLES_OK: {
            // SELECT — build columns + rows.
            int nCols = PQnfields(res);
            int nRows = PQntuples(res);

            // Column names.
            result.columns.reserve(nCols);
            for (int c = 0; c < nCols; ++c) {
                result.columns.emplace_back(PQfname(res, c));
            }

            // Row data.
            result.rows.reserve(nRows);
            for (int r = 0; r < nRows; ++r) {
                std::vector<alfi::db::Value> row;
                row.reserve(nCols);

                for (int c = 0; c < nCols; ++c) {
                    if (PQgetisnull(res, r, c)) {
                        row.emplace_back(alfi::db::Value::null());
                    } else {
                        Oid oid = PQftype(res, c);
                        const char* text = PQgetvalue(res, r, c);
                        row.emplace_back(pgValueFromText(oid, text));
                    }
                }

                result.rows.push_back(std::move(row));
            }

            result.affectedRows = static_cast<std::size_t>(nRows);
            PQclear(res);
            return result;
        }

        default: {
            // Error — extract SQLSTATE and message.
            std::string errorMsg = PQresultErrorMessage(res);
            const char* sqlstate = PQresultErrorField(res, PG_DIAG_SQLSTATE);
            std::string code = sqlstate ? sqlstate : "QUERY_FAILURE";
            PQclear(res);
            throw alfi::db::DbError(
                "Query failed: " + errorMsg,
                code);
        }
    }
}

// ---- Transaction control --------------------------------------------------

void PgConnection::executeSimple(const std::string& sql) {
    if (!conn_) {
        throw alfi::db::DbError("Connection is closed", "CONNECTION_CLOSED");
    }

    PGresult* res = PQexec(conn_, sql.c_str());

    if (!res) {
        throw alfi::db::DbError(
            "PQexec returned null: " + std::string(PQerrorMessage(conn_)),
            "QUERY_FAILURE");
    }

    ExecStatusType status = PQresultStatus(res);
    if (status != PGRES_COMMAND_OK) {
        std::string errorMsg = PQresultErrorMessage(res);
        const char* sqlstate = PQresultErrorField(res, PG_DIAG_SQLSTATE);
        std::string code = sqlstate ? sqlstate : "TRANSACTION_FAILURE";
        PQclear(res);
        throw alfi::db::DbError(
            "Transaction command failed: " + errorMsg,
            code);
    }

    PQclear(res);
}

void PgConnection::beginTransaction() {
    executeSimple("BEGIN");
}

void PgConnection::commit() {
    executeSimple("COMMIT");
}

void PgConnection::rollback() {
    executeSimple("ROLLBACK");
}

} // namespace chatterpg

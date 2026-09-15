/// @file test_pg_connection.cpp
/// Integration tests for chatter-pg against a real PostgreSQL instance.
///
/// Requires CHATTERPG_TEST_DSN environment variable to be set, e.g.:
///   export CHATTERPG_TEST_DSN="postgres://testuser:testpass@localhost:5432/testdb"
///
/// If the env var is not set, ALL tests in this file are SKIPPED (not failed).

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include <chatterpg/chatterpg.hpp>
#include <alfi/db/db_error.hpp>
#include <alfi/db/driver_registry.hpp>
#include <alfi/db/param.hpp>
#include <alfi/db/value.hpp>

using namespace alfi::db;

// ---------------------------------------------------------------------------
// Helper: get DSN from environment, or SKIP if not set.
// ---------------------------------------------------------------------------
static std::string getDsn() {
    const char* dsn = std::getenv("CHATTERPG_TEST_DSN");
    if (!dsn || dsn[0] == '\0') {
        SKIP("CHATTERPG_TEST_DSN not set — skipping integration tests. "
             "Set it to a postgres://… DSN pointing at a test database.");
    }
    return std::string(dsn);
}

// ---------------------------------------------------------------------------
// Helper: get a connected PgConnection.
// ---------------------------------------------------------------------------
static std::unique_ptr<Connection> connect() {
    chatterpg::PgDriver driver;
    return driver.connect(getDsn());
}

// ---------------------------------------------------------------------------
// Helper: ensure test table exists and is clean.
// ---------------------------------------------------------------------------
static void ensureTestTable(Connection& conn) {
    conn.execute("DROP TABLE IF EXISTS chatterpg_test");
    conn.execute(
        "CREATE TABLE chatterpg_test ("
        "  id SERIAL PRIMARY KEY,"
        "  name TEXT,"
        "  age INT,"
        "  score DOUBLE PRECISION,"
        "  active BOOLEAN,"
        "  bio TEXT"
        ")");
}

// ===========================================================================
// Tests
// ===========================================================================

TEST_CASE("PgDriver::name() returns postgres", "[driver]") {
    chatterpg::PgDriver driver;
    REQUIRE(driver.name() == "postgres");
}

TEST_CASE("Successful connection", "[connection]") {
    auto conn = connect();
    REQUIRE(conn != nullptr);

    // Smoke test: run a trivial query.
    auto result = conn->execute("SELECT 1 AS one");
    REQUIRE(result.rowCount() == 1);
    REQUIRE(result.columns.size() == 1);
    REQUIRE(result.columns[0] == "one");
    REQUIRE(result[0][0].asInt() == 1);
}

TEST_CASE("Connection failure with bad DSN throws DbError", "[connection][error]") {
    chatterpg::PgDriver driver;
    REQUIRE_THROWS_AS(
        driver.connect("postgres://nobody:wrong@127.0.0.1:1/nonexistent"),
        DbError);
}

TEST_CASE("Parameterized INSERT and SELECT round-trip", "[query]") {
    auto conn = connect();
    ensureTestTable(*conn);

    // INSERT
    conn->execute(
        "INSERT INTO chatterpg_test (name, age, score, active, bio) "
        "VALUES ($1, $2, $3, $4, $5)",
        {Param("Alice"), Param(30), Param(95.5), Param(true), Param("A bio")});

    conn->execute(
        "INSERT INTO chatterpg_test (name, age, score, active, bio) "
        "VALUES ($1, $2, $3, $4, $5)",
        {Param("Bob"), Param(25), Param(82.3), Param(false), Param::null()});

    // SELECT all
    auto result = conn->execute(
        "SELECT name, age, score, active, bio FROM chatterpg_test ORDER BY name");

    REQUIRE(result.rowCount() == 2);

    // Alice
    REQUIRE(result[0][0].asString() == "Alice");
    REQUIRE(result[0][1].asInt() == 30);
    REQUIRE(result[0][2].asDouble() == Catch::Approx(95.5));
    REQUIRE(result[0][3].asBool() == true);
    REQUIRE(result[0][4].asString() == "A bio");

    // Bob
    REQUIRE(result[1][0].asString() == "Bob");
    REQUIRE(result[1][1].asInt() == 25);
    REQUIRE(result[1][2].asDouble() == Catch::Approx(82.3));
    REQUIRE(result[1][3].asBool() == false);
    REQUIRE(result[1][4].isNull());

    // Parameterized SELECT
    auto filtered = conn->execute(
        "SELECT name FROM chatterpg_test WHERE age > $1",
        {Param(27)});
    REQUIRE(filtered.rowCount() == 1);
    REQUIRE(filtered[0][0].asString() == "Alice");

    // Cleanup
    conn->execute("DROP TABLE IF EXISTS chatterpg_test");
}

TEST_CASE("NULL value handling", "[query][null]") {
    auto conn = connect();
    ensureTestTable(*conn);

    // Insert a row with several NULLs.
    conn->execute(
        "INSERT INTO chatterpg_test (name, age, score, active, bio) "
        "VALUES ($1, $2, $3, $4, $5)",
        {Param::null(), Param::null(), Param::null(), Param::null(), Param::null()});

    auto result = conn->execute(
        "SELECT name, age, score, active, bio FROM chatterpg_test");

    REQUIRE(result.rowCount() == 1);
    for (std::size_t c = 0; c < result.columns.size(); ++c) {
        REQUIRE(result[0][c].isNull());
    }

    conn->execute("DROP TABLE IF EXISTS chatterpg_test");
}

TEST_CASE("Query syntax error throws DbError", "[query][error]") {
    auto conn = connect();
    REQUIRE_THROWS_AS(
        conn->execute("SELEKT * FROM nonexistent_table"),
        DbError);
}

TEST_CASE("Transaction BEGIN + COMMIT persists data", "[transaction]") {
    auto conn = connect();
    ensureTestTable(*conn);

    conn->beginTransaction();
    conn->execute(
        "INSERT INTO chatterpg_test (name, age) VALUES ($1, $2)",
        {Param("Carol"), Param(40)});
    conn->commit();

    // Data should persist after commit.
    auto result = conn->execute(
        "SELECT name FROM chatterpg_test WHERE name = $1",
        {Param("Carol")});
    REQUIRE(result.rowCount() == 1);
    REQUIRE(result[0][0].asString() == "Carol");

    conn->execute("DROP TABLE IF EXISTS chatterpg_test");
}

TEST_CASE("Transaction BEGIN + ROLLBACK undoes write", "[transaction]") {
    auto conn = connect();
    ensureTestTable(*conn);

    // Insert baseline data outside of the transaction we'll rollback.
    conn->execute(
        "INSERT INTO chatterpg_test (name, age) VALUES ($1, $2)",
        {Param("Dan"), Param(50)});

    conn->beginTransaction();
    conn->execute(
        "INSERT INTO chatterpg_test (name, age) VALUES ($1, $2)",
        {Param("Eve"), Param(28)});

    // Verify Eve is visible inside the transaction.
    auto during = conn->execute(
        "SELECT name FROM chatterpg_test WHERE name = $1",
        {Param("Eve")});
    REQUIRE(during.rowCount() == 1);

    conn->rollback();

    // Eve should NOT exist after rollback.
    auto after = conn->execute(
        "SELECT name FROM chatterpg_test WHERE name = $1",
        {Param("Eve")});
    REQUIRE(after.rowCount() == 0);

    // Dan (committed before the transaction) should still exist.
    auto dan = conn->execute(
        "SELECT name FROM chatterpg_test WHERE name = $1",
        {Param("Dan")});
    REQUIRE(dan.rowCount() == 1);

    conn->execute("DROP TABLE IF EXISTS chatterpg_test");
}

TEST_CASE("DriverRegistry integration", "[driver][registry]") {
    // Register PgDriver and retrieve it by name.
    DriverRegistry::registerDriver(std::make_shared<chatterpg::PgDriver>());

    auto driver = DriverRegistry::get("postgres");
    REQUIRE(driver != nullptr);
    REQUIRE(driver->name() == "postgres");

    // Can connect through the registry.
    auto conn = driver->connect(getDsn());
    REQUIRE(conn != nullptr);

    auto result = conn->execute("SELECT 42 AS answer");
    REQUIRE(result.rowCount() == 1);
    REQUIRE(result[0][0].asInt() == 42);
}

TEST_CASE("Type mapping: int2, int4, int8", "[query][types]") {
    auto conn = connect();

    auto result = conn->execute(
        "SELECT 1::smallint AS s, 2::integer AS i, 3::bigint AS b");
    REQUIRE(result.rowCount() == 1);
    REQUIRE(result[0][0].asInt() == 1);
    REQUIRE(result[0][1].asInt() == 2);
    REQUIRE(result[0][2].asInt() == 3);
}

TEST_CASE("Type mapping: float4, float8", "[query][types]") {
    auto conn = connect();

    auto result = conn->execute(
        "SELECT 1.5::real AS r, 2.5::double precision AS d");
    REQUIRE(result.rowCount() == 1);
    REQUIRE(result[0][0].asDouble() == Catch::Approx(1.5));
    REQUIRE(result[0][1].asDouble() == Catch::Approx(2.5));
}

TEST_CASE("Type mapping: boolean", "[query][types]") {
    auto conn = connect();

    auto result = conn->execute("SELECT true AS t, false AS f");
    REQUIRE(result.rowCount() == 1);
    REQUIRE(result[0][0].asBool() == true);
    REQUIRE(result[0][1].asBool() == false);
}

TEST_CASE("Type mapping: text and varchar", "[query][types]") {
    auto conn = connect();

    auto result = conn->execute(
        "SELECT 'hello'::text AS t, 'world'::varchar(50) AS v");
    REQUIRE(result.rowCount() == 1);
    REQUIRE(result[0][0].asString() == "hello");
    REQUIRE(result[0][1].asString() == "world");
}

TEST_CASE("affectedRows on INSERT/UPDATE/DELETE", "[query]") {
    auto conn = connect();
    ensureTestTable(*conn);

    auto ins = conn->execute(
        "INSERT INTO chatterpg_test (name, age) VALUES ($1, $2)",
        {Param("Fay"), Param(33)});
    REQUIRE(ins.affectedRows == 1);

    auto upd = conn->execute(
        "UPDATE chatterpg_test SET age = $1 WHERE name = $2",
        {Param(34), Param("Fay")});
    REQUIRE(upd.affectedRows == 1);

    auto del = conn->execute(
        "DELETE FROM chatterpg_test WHERE name = $1",
        {Param("Fay")});
    REQUIRE(del.affectedRows == 1);

    conn->execute("DROP TABLE IF EXISTS chatterpg_test");
}

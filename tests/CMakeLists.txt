# tests/CMakeLists.txt
#
# Integration tests for chatter-pg.
#
# These tests require a real PostgreSQL instance. They do NOT mock libpq.
#
# === How to run ===
#
# 1. Start a Postgres instance (Docker is easiest):
#
#    docker run --name chatterpg-test-db \
#      -e POSTGRES_USER=testuser \
#      -e POSTGRES_PASSWORD=testpass \
#      -e POSTGRES_DB=testdb \
#      -p 5432:5432 \
#      -d postgres:16
#
# 2. Set the DSN environment variable:
#
#    export CHATTERPG_TEST_DSN="postgres://testuser:testpass@localhost:5432/testdb"
#
# 3. Build and run:
#
#    cmake --build build && ctest --test-dir build --output-on-failure
#
# If CHATTERPG_TEST_DSN is not set, the tests will SKIP (not fail),
# so CI without a Postgres service just shows them as skipped.

find_package(Catch2 3 QUIET)
if(NOT Catch2_FOUND)
    # Catch2 was fetched via FetchContent in the parent — it should be
    # available as an imported target already.
endif()

add_executable(test_pg_connection
    test_pg_connection.cpp
)

target_link_libraries(test_pg_connection
    PRIVATE
        chatterpg
        Catch2::Catch2WithMain
)

include(CTest)
include(Catch)
catch_discover_tests(test_pg_connection)

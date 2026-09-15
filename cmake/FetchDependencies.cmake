# cmake/FetchDependencies.cmake
# Fetches external dependencies for chatter-pg.
#
# 1. Alfi core — header-only db/ interface (Driver, Connection, etc.)
# 2. libpq     — PostgreSQL C client library (system dependency via pkg-config)
# 3. Catch2    — test framework (only when tests are enabled)

cmake_policy(SET CMP0169 OLD)   # Allow FetchContent_Populate for header-only fetch

include(FetchContent)
find_package(PkgConfig REQUIRED)

# ---------------------------------------------------------------------------
# Alfi core (db/ interface headers only)
# ---------------------------------------------------------------------------
# We fetch the full Alfi core repo but only consume its include/ directory
# as a header-only INTERFACE library. We do NOT build alfi's own targets
# (it has HTTP/router code and dependencies we don't need).
#
# Pin to a specific commit so chatter-pg isn't silently broken by future
# alfi core changes. Update the GIT_TAG when adopting a new interface version.
FetchContent_Declare(
    alfi_core
    GIT_REPOSITORY https://github.com/AbdulKadir-22/Alfi.git
    GIT_TAG        1c63ebee3adad43b9270edd5bd29b50bf0307a03
)

# Populate without calling add_subdirectory — we only need the headers.
FetchContent_GetProperties(alfi_core)
if(NOT alfi_core_POPULATED)
    FetchContent_Populate(alfi_core)
endif()

# Create a header-only INTERFACE target for the db/ headers.
add_library(alfi_db_interface INTERFACE)
target_include_directories(alfi_db_interface INTERFACE
    ${alfi_core_SOURCE_DIR}/include
)

# ---------------------------------------------------------------------------
# libpq (PostgreSQL C client library) — must be installed on the system
# ---------------------------------------------------------------------------
pkg_check_modules(LIBPQ REQUIRED IMPORTED_TARGET libpq)

# ---------------------------------------------------------------------------
# Catch2 (test framework) — fetched only when tests are built
# ---------------------------------------------------------------------------
if(CHATTERPG_BUILD_TESTS)
    FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG        v3.4.0
    )
    FetchContent_MakeAvailable(Catch2)

    # Make Catch2's CMake helpers (catch_discover_tests) available.
    list(APPEND CMAKE_MODULE_PATH ${Catch2_SOURCE_DIR}/extras)
endif()

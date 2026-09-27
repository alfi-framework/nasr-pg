cmake_minimum_required(VERSION 3.14)
project(nasr-pg VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- Options ----------------------------------------------------------------
option(NASRPG_BUILD_TESTS "Build nasr-pg integration tests" ON)

# --- Dependencies -----------------------------------------------------------
include(cmake/FetchDependencies.cmake)

# --- Library ----------------------------------------------------------------
add_library(nasrpg
    src/nasrpg/pg_driver.cpp
    src/nasrpg/pg_connection.cpp
)

target_include_directories(nasrpg PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# Link against alfi core (header-only db/ interface) and libpq.
target_link_libraries(nasrpg PUBLIC alfi_db_interface)
target_link_libraries(nasrpg PRIVATE PkgConfig::LIBPQ)

# --- Tests ------------------------------------------------------------------
if(NASRPG_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

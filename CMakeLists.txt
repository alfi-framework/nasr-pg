cmake_minimum_required(VERSION 3.14)
project(chatter-pg VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- Options ----------------------------------------------------------------
option(CHATTERPG_BUILD_TESTS "Build chatter-pg integration tests" ON)

# --- Dependencies -----------------------------------------------------------
include(cmake/FetchDependencies.cmake)

# --- Library ----------------------------------------------------------------
add_library(chatterpg
    src/chatterpg/pg_driver.cpp
    src/chatterpg/pg_connection.cpp
)

target_include_directories(chatterpg PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# Link against alfi core (header-only db/ interface) and libpq.
target_link_libraries(chatterpg PUBLIC alfi_db_interface)
target_link_libraries(chatterpg PRIVATE PkgConfig::LIBPQ)

# --- Tests ------------------------------------------------------------------
if(CHATTERPG_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

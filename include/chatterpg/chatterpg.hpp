#pragma once

/// @file chatterpg.hpp
/// Umbrella header for the chatter-pg PostgreSQL driver.
///
/// Including this single header gives you everything needed to register
/// and use the Postgres driver with Alfi's database layer:
///
///   #include <chatterpg/chatterpg.hpp>
///
///   chatterpg::PgDriver driver;
///   auto conn = driver.connect("postgres://user:pass@localhost:5432/mydb");
///   auto result = conn->execute("SELECT 1");

#include <chatterpg/pg_driver.hpp>
#include <chatterpg/pg_connection.hpp>

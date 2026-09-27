#pragma once

/// @file nasrpg.hpp
/// Umbrella header for the nasr-pg PostgreSQL driver.
///
/// Including this single header gives you everything needed to register
/// and use the Postgres driver with Alfi's database layer:
///
///   #include <nasrpg/nasrpg.hpp>
///
///   nasrpg::PgDriver driver;
///   auto conn = driver.connect("postgres://user:pass@localhost:5432/mydb");
///   auto result = conn->execute("SELECT 1");

#include <nasrpg/pg_driver.hpp>
#include <nasrpg/pg_connection.hpp>

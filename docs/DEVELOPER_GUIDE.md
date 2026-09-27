# Developer & Maintainer Guide for nasr-pg

This guide is intended for developers contributing to, debugging, or extending **nasr-pg**.

---

## 1. Codebase Architecture & File Structure

```
nasr-pg/
├── CMakeLists.txt                 # Main CMake project file
├── cmake/
│   └── FetchDependencies.cmake    # Fetches Alfi core headers and Catch2 via FetchContent
├── include/
│   └── nasrpg/
│       ├── nasrpg.hpp          # Main umbrella header
│       ├── pg_driver.hpp          # PgDriver class definition
│       └── pg_connection.hpp      # PgConnection class definition
├── src/
│   └── nasrpg/
│       ├── pg_driver.cpp          # Driver implementation (PQconnectdb wrapper)
│       └── pg_connection.cpp      # Connection implementation (PQexecParams, transactions, mapping)
├── tests/
│   ├── CMakeLists.txt             # Test build rules
│   └── test_pg_connection.cpp     # Catch2 integration tests
├── docs/                          # Comprehensive documentation
│   ├── OVERVIEW.md
│   ├── USER_GUIDE.md
│   ├── DEVELOPER_GUIDE.md
│   ├── ARCHITECTURE.md
│   ├── CONNECTION_AND_POOLING.md
│   ├── ERROR_HANDLING.md
│   └── DECISIONS.md
└── README.md
```

---

## 2. Dependencies & Build Instructions

### Dependencies
1. **Alfi Core** (`alfi_core`): Fetched at configure time via `FetchContent` in `cmake/FetchDependencies.cmake`. The headers are extracted and wrapped into an `INTERFACE` library target (`alfi_db_interface`).
2. **libpq**: Located on the build system via `pkg_check_modules(LIBPQ REQUIRED IMPORTED_TARGET libpq)`.
3. **Catch2**: Fetched at build time when `NASRPG_BUILD_TESTS=ON` (default).

### Building nasr-pg Locally

```bash
# Clone repository
git clone https://github.com/alfi-framework/nasr-pg.git
cd nasr-pg

# Configure build directory
cmake -B build -DCMAKE_BUILD_TYPE=Debug

# Build library and tests
cmake --build build
```

---

## 3. Running Integration Tests

`nasr-pg` tests run against a live PostgreSQL instance.

### Setting up Postgres via Docker

```bash
docker run --name nasrpg-test-db \
  -e POSTGRES_USER=testuser \
  -e POSTGRES_PASSWORD=testpass \
  -e POSTGRES_DB=testdb \
  -p 5432:5432 \
  -d postgres:16
```

### Running Test Suite

Set the environment variable `NASRPG_TEST_DSN` and execute `ctest`:

```bash
export NASRPG_TEST_DSN="postgres://testuser:testpass@localhost:5432/testdb"
ctest --test-dir build --output-on-failure
```

> **Note**: If `NASRPG_TEST_DSN` is not set, Catch2 tests automatically trigger `SKIP()`, ensuring builds do not fail in environments lacking a database.

---

## 4. How Data Types are Handled & Extended

### Parameter Serialization (`paramToString`)

When passing `alfi::db::Param` to `PQexecParams`, parameters are converted to text strings inside `src/nasrpg/pg_connection.cpp`:

```cpp
static std::string paramToString(const alfi::db::Param& p, bool& isNull)
```

- `bool`: Serialized as `"t"` or `"f"`.
- `int64_t`: Serialized via `std::to_string()`.
- `double`: Serialized via `std::to_string()`.
- `std::string`: Passed directly.
- `null`: `isNull` flag set to `true`, `nullptr` passed to libpq.

### Result Type Conversion (`pgValueFromText`)

PostgreSQL column values are retrieved in text format (`resultFormat = 0`). The helper function `pgValueFromText` checks the PostgreSQL column Object Identifier (**OID**):

```cpp
static alfi::db::Value pgValueFromText(Oid oid, const char* text)
```

#### Supported OID Mappings

| Postgres Type | OID | Internal Handler | Result `alfi::db::Value` Variant |
|---|---|---|---|
| `bool` | 16 | `text[0] == 't'` | `bool` |
| `int2` (smallint) | 21 | `std::strtoll` | `int64_t` |
| `int4` (integer) | 23 | `std::strtoll` | `int64_t` |
| `int8` (bigint) | 20 | `std::strtoll` | `int64_t` |
| `oid` | 26 | `std::strtoll` | `int64_t` |
| `float4` (real) | 700 | `std::strtod` | `double` |
| `float8` (double precision) | 701 | `std::strtod` | `double` |
| `numeric` | 1700 | `std::strtod` | `double` (best effort) |
| `text` | 25 | Direct string construct | `std::string` |
| `varchar` | 1043 | Direct string construct | `std::string` |
| `bpchar` (`char(n)`) | 1042 | Direct string construct | `std::string` |
| `name` | 19 | Direct string construct | `std::string` |
| *All others* | *any* | Default switch branch | `std::string` (lossless text representation) |

### How to Add Support for New Types
1. Identify the PostgreSQL OID from `pg_type.h` (or inspect via `SELECT oid, typname FROM pg_type`).
2. Add a `case` statement to `pgValueFromText` in `src/nasrpg/pg_connection.cpp`.
3. Add corresponding test cases in `tests/test_pg_connection.cpp`.

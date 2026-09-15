# chatter-pg

A standalone PostgreSQL driver for the [Alfi](https://github.com/AbdulKadir-22/Alfi) HTTP framework's database layer.

Implements `alfi::db::Driver` and `alfi::db::Connection` backed by [libpq](https://www.postgresql.org/docs/current/libpq.html), so any Alfi application can connect to PostgreSQL with parameterized queries, type-safe results, and transaction support.

## Features

- **Parameterized queries** — all parameters go through `PQexecParams`; SQL injection is structurally impossible
- **Type-safe results** — Postgres types mapped to `alfi::db::Value` variants (`int64_t`, `double`, `bool`, `string`, `null`)
- **RAII connection management** — `PGconn*` cleaned up automatically
- **Transaction support** — `beginTransaction()`, `commit()`, `rollback()`
- **Error handling** — all failures surface as `alfi::db::DbError` with SQLSTATE codes

## Prerequisites

- **C++17** compiler (GCC 7+, Clang 5+, MSVC 19.14+)
- **CMake 3.14+**
- **libpq-dev** (PostgreSQL client library)
  - Ubuntu/Debian: `sudo apt install libpq-dev pkg-config`
  - Fedora/RHEL: `sudo dnf install libpq-devel`
  - macOS (Homebrew): `brew install libpq && brew link libpq`
  - Arch: `sudo pacman -S postgresql-libs`
- **PostgreSQL** instance (for running tests)

## Build

```bash
git clone https://github.com/AbdulKadir-22/chatter-pg.git
cd chatter-pg

cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

## Usage

```cpp
#include <chatterpg/chatterpg.hpp>
#include <alfi/db/driver_registry.hpp>
#include <alfi/db/param.hpp>

using namespace alfi::db;

int main() {
    // Register the Postgres driver.
    DriverRegistry::registerDriver(std::make_shared<chatterpg::PgDriver>());

    // Connect via the registry (or directly via PgDriver::connect).
    auto conn = DriverRegistry::get("postgres")
        ->connect("postgres://user:pass@localhost:5432/mydb");

    // Parameterized query — never interpolates params into SQL.
    auto result = conn->execute(
        "SELECT name, age FROM users WHERE id = $1",
        {Param(42)});

    for (std::size_t i = 0; i < result.rowCount(); ++i) {
        std::string name = result[i][0].asString();
        int64_t age = result[i][1].asInt();
        // ...
    }

    // Transactions.
    conn->beginTransaction();
    conn->execute(
        "INSERT INTO users (name, age) VALUES ($1, $2)",
        {Param("Alice"), Param(30)});
    conn->commit();  // or conn->rollback();

    return 0;
}
```

### CMake Integration

To use chatter-pg in your own CMake project:

```cmake
FetchContent_Declare(
    chatterpg
    GIT_REPOSITORY https://github.com/AbdulKadir-22/chatter-pg.git
    GIT_TAG        main  # or a specific commit/tag
)
FetchContent_MakeAvailable(chatterpg)

target_link_libraries(your_app PRIVATE chatterpg)
```

## Running Tests

Tests require a real PostgreSQL instance:

```bash
# 1. Start Postgres (Docker)
docker run --name chatterpg-test-db \
  -e POSTGRES_USER=testuser \
  -e POSTGRES_PASSWORD=testpass \
  -e POSTGRES_DB=testdb \
  -p 5432:5432 \
  -d postgres:16

# 2. Set the DSN
export CHATTERPG_TEST_DSN="postgres://testuser:testpass@localhost:5432/testdb"

# 3. Build with tests enabled (default)
cmake -B build
cmake --build build

# 4. Run
ctest --test-dir build --output-on-failure
```

If `CHATTERPG_TEST_DSN` is not set, tests **skip** rather than fail.

## Project Structure

```
chatter-pg/
├── CMakeLists.txt                    # Root build config
├── cmake/
│   └── FetchDependencies.cmake       # Fetches alfi core + Catch2, finds libpq
├── include/
│   └── chatterpg/
│       ├── chatterpg.hpp             # Umbrella header
│       ├── pg_driver.hpp             # PgDriver : alfi::db::Driver
│       └── pg_connection.hpp         # PgConnection : alfi::db::Connection
├── src/
│   └── chatterpg/
│       ├── pg_driver.cpp
│       └── pg_connection.cpp
├── tests/
│   ├── CMakeLists.txt
│   └── test_pg_connection.cpp        # Integration tests (real Postgres)
├── docs/
│   └── DECISIONS.md                  # Design decisions & type mapping
├── README.md
└── LICENSE
```

## Design Decisions

See [docs/DECISIONS.md](docs/DECISIONS.md) for:

- Pinned alfi core version
- PostgreSQL → Value type mapping table
- Deliberately unsupported types (arrays, jsonb, timestamps, etc.)
- Transaction implementation details
- Interface fitness notes (pooling, prepared statements, async)

## License

[MIT](LICENSE)

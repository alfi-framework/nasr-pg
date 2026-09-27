# User & Consumer Guide for nasr-pg

This guide details how to integrate and consume **nasr-pg** in an application built on top of the [Alfi](https://github.com/AbdulKadir-22/Alfi) HTTP framework.

---

## 1. Installation & Integration

### Prerequisites
- **C++17 Compiler** (GCC 7+, Clang 5+, MSVC 2017+)
- **CMake 3.14+**
- **libpq** development libraries installed on the system:
  - **Ubuntu / Debian**: `sudo apt install libpq-dev pkg-config`
  - **Fedora / RHEL**: `sudo dnf install libpq-devel pkg-config`
  - **Arch Linux**: `sudo pacman -S postgresql-libs pkgconf`
  - **macOS**: `brew install libpq && brew link libpq`

### CMake Integration via `FetchContent`

Add the following snippet to your project's `CMakeLists.txt`:

```cmake
include(FetchContent)

FetchContent_Declare(
    nasrpg
    GIT_REPOSITORY https://github.com/alfi-framework/nasr-pg.git
    GIT_TAG        main # Or a specific tag/commit hash
)
FetchContent_MakeAvailable(nasrpg)

# Link nasrpg to your target
target_link_libraries(your_application PRIVATE nasrpg)
```

---

## 2. Basic Usage

### Include Headers

Include the umbrella header to gain access to driver registration and connection classes:

```cpp
#include <nasrpg/nasrpg.hpp>
#include <alfi/db/driver_registry.hpp>
#include <alfi/db/param.hpp>
#include <iostream>
```

### Driver Registration & Connection

You can connect directly using `nasrpg::PgDriver` or via Alfi's central `DriverRegistry`:

#### Direct Usage
```cpp
nasrpg::PgDriver driver;
std::unique_ptr<alfi::db::Connection> conn = 
    driver.connect("postgres://user:password@localhost:5432/my_database");
```

#### DriverRegistry Usage (Recommended for Alfi Apps)
```cpp
// Register driver under name "postgres"
alfi::db::DriverRegistry::registerDriver(std::make_shared<nasrpg::PgDriver>());

// Retrieve driver and establish connection
auto conn = alfi::db::DriverRegistry::get("postgres")
    ->connect("postgres://user:password@localhost:5432/my_database");
```

---

## 3. Executing Queries

### Dynamic / Parameterized Queries (SELECT)

All query parameters must be passed using `alfi::db::Param`. Placeholders use PostgreSQL positional syntax (`$1`, `$2`, etc.).

```cpp
try {
    auto result = conn->execute(
        "SELECT id, name, score, active FROM users WHERE age >= $1 AND status = $2",
        { alfi::db::Param(21), alfi::db::Param("active") }
    );

    std::cout << "Returned " << result.rowCount() << " rows.\n";

    for (std::size_t i = 0; i < result.rowCount(); ++i) {
        int64_t id = result[i][0].asInt();
        std::string name = result[i][1].asString();
        double score = result[i][2].asDouble();
        bool active = result[i][3].asBool();

        std::cout << "User #" << id << ": " << name 
                  << " (Score: " << score << ", Active: " << active << ")\n";
    }
} catch (const alfi::db::DbError& err) {
    std::cerr << "Database Error: " << err.what() << " [Code: " << err.code() << "]\n";
}
```

### Executing Mutations (INSERT / UPDATE / DELETE)

For non-`SELECT` statements, `execute()` returns a `QueryResult` containing `affectedRows`:

```cpp
auto res = conn->execute(
    "UPDATE users SET score = score + $1 WHERE active = $2",
    { alfi::db::Param(10.5), alfi::db::Param(true) }
);

std::cout << "Updated " << res.affectedRows << " rows.\n";
```

### Working with NULL Values

To bind a NULL parameter:
```cpp
conn->execute(
    "INSERT INTO users (name, bio) VALUES ($1, $2)",
    { alfi::db::Param("Alice"), alfi::db::Param::null() }
);
```

To check for NULL values in result sets:
```cpp
auto res = conn->execute("SELECT bio FROM users WHERE name = $1", { alfi::db::Param("Alice") });
if (res[0][0].isNull()) {
    std::cout << "User bio is NULL\n";
}
```

---

## 4. Transaction Management

`nasr-pg` provides explicit transaction boundaries:

```cpp
try {
    conn->beginTransaction();

    conn->execute(
        "UPDATE accounts SET balance = balance - $1 WHERE id = $2",
        { alfi::db::Param(100.0), alfi::db::Param(1) }
    );

    conn->execute(
        "UPDATE accounts SET balance = balance + $1 WHERE id = $2",
        { alfi::db::Param(100.0), alfi::db::Param(2) }
    );

    conn->commit();
} catch (const alfi::db::DbError& e) {
    conn->rollback();
    std::cerr << "Transaction aborted due to error: " << e.what() << "\n";
}
```

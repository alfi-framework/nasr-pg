# nasr-pg Documentation Overview

Welcome to the documentation for **nasr-pg**, the official PostgreSQL database driver implementation for the **[Alfi](https://github.com/AbdulKadir-22/Alfi)** C++ HTTP framework.

## Documentation Index

| Document | Content Summary | Target Audience |
|----------|-----------------|-----------------|
| **[USER_GUIDE.md](USER_GUIDE.md)** | Getting started, CMake integration via `FetchContent`, code examples, connection strings, query execution, transaction management. | Application Developers / Library Consumers |
| **[DEVELOPER_GUIDE.md](DEVELOPER_GUIDE.md)** | Architecture overview, build system, dependency management, running integration tests, type mapping details, extending the codebase. | Driver Maintainers / Contributors |
| **[ARCHITECTURE.md](ARCHITECTURE.md)** | Detailed architecture, component design, query execution pipeline, complete job catalog (every single job performed by nasr-pg). | Architects & Developers |
| **[CONNECTION_AND_POOLING.md](CONNECTION_AND_POOLING.md)** | Deep dive into how PostgreSQL connections are established via libpq, connection lifecycle, RAII patterns, and connection pooling analysis. | Developers & System Engineers |
| **[ERROR_HANDLING.md](ERROR_HANDLING.md)** | Comprehensive list of error handling mechanisms, exception safety guarantees, error classification, and SQLSTATE mapping. | All Developers |
| **[DECISIONS.md](DECISIONS.md)** | Architectural Decision Records (ADRs), pinned dependency versions, type mapping specifications, and trade-offs. | Maintainers |

---

## What is nasr-pg?

`nasr-pg` is a lightweight, standalone C++17 driver that bridges the PostgreSQL C library (`libpq`) with the unified database abstraction layer (`alfi::db`) provided by the Alfi framework.

It enables Alfi applications to interact with PostgreSQL databases seamlessly through standard interfaces:
- **`alfi::db::Driver`** — Implemented by `nasrpg::PgDriver`
- **`alfi::db::Connection`** — Implemented by `nasrpg::PgConnection`

### Key Highlights
- **100% Parameterized Execution**: All dynamic SQL queries are executed via `PQexecParams` to eliminate SQL injection vulnerabilities.
- **Strict Exception-Based Error Model**: Converts libpq C-style return status codes and PostgreSQL SQLSTATE codes into `alfi::db::DbError` C++ exceptions.
- **RAII Connection Ownership**: Manages underlying `PGconn*` resources automatically using move-only semantics.
- **Zero Overhead Abstraction**: Direct text-format serialization and parsing without external ORM bloat.

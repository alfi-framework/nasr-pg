# DECISIONS.md — chatter-pg Design Decisions

## Pinned Alfi Core Version

| Field    | Value |
|----------|-------|
| Repo     | `https://github.com/AbdulKadir-22/Alfi.git` |
| Commit   | `1c63ebee3adad43b9270edd5bd29b50bf0307a03` |
| Branch   | `main` |
| Message  | `feat(db) : Phase 1 & 2 database layer implementation and example integration` |
| Date     | 2026-03-14 |
| Tags     | *(none — no tags exist in the repo yet)* |

> **NOTE**: When alfi core creates a release tag (e.g. `v0.1.0`), update
> `GIT_TAG` in `cmake/FetchDependencies.cmake` to pin to the tag instead
> of a raw commit hash.

---

## Connection String Format

PgDriver::connect() passes the DSN directly to libpq's `PQconnectdb()`.
This means it accepts **any format libpq supports**:

- **URI form**: `postgres://user:pass@host:port/dbname`
- **Key-value form**: `host=localhost port=5432 dbname=mydb user=me password=secret`

We do **not** parse or validate the DSN ourselves — libpq handles all of it,
including SSL parameters, timeouts, etc.

---

## Type Mapping Table

PostgreSQL types are identified by their OID (from `pg_type`). We use
text-format results (`resultFormat=0` in `PQexecParams`) and parse the
text representation.

| Postgres Type      | OID  | Value Variant   | Notes |
|--------------------|------|-----------------|-------|
| `boolean`          | 16   | `bool`          | `'t'` → true, `'f'` → false |
| `smallint` (int2)  | 21   | `int64_t`       | Widened to int64 |
| `integer` (int4)   | 23   | `int64_t`       | Widened to int64 |
| `bigint` (int8)    | 20   | `int64_t`       | Native fit |
| `oid`              | 26   | `int64_t`       | Treated as integer |
| `real` (float4)    | 700  | `double`        | Widened to double |
| `double precision` | 701  | `double`        | Native fit |
| `numeric`          | 1700 | `double`        | **Lossy** — see below |
| `text`             | 25   | `std::string`   | |
| `varchar`          | 1043 | `std::string`   | |
| `char(n)` (bpchar) | 1042 | `std::string`   | Includes trailing spaces |
| `name`             | 19   | `std::string`   | Postgres internal type |
| `NULL`             | any  | `std::monostate`| Via `PQgetisnull()` |
| *(any other OID)*  | —    | `std::string`   | Fallback: raw text repr |

### Numeric/Decimal Caveat

`numeric` / `decimal` is mapped to `double`, which is **lossy** for values
with more than ~15 significant digits. This is a deliberate trade-off:
Value's variant doesn't include a decimal type yet. For exact arithmetic,
use text (cast to `::text` in SQL, then parse client-side).

---

## Deliberately Unsupported Types (Future Work)

These Postgres types are **not** mapped to specific Value variants. They
fall through to the string fallback (the raw text representation is preserved,
so no data is lost — it's just not parsed into a typed variant).

| Type          | OID(s)        | Reason / Future Plan |
|---------------|---------------|----------------------|
| `bytea`       | 17            | Value has no binary variant yet |
| `date`        | 1082          | Value has no date variant yet |
| `timestamp`   | 1114, 1184    | Value has no timestamp variant |
| `time`        | 1083, 1266    | Value has no time variant |
| `interval`    | 1186          | Value has no interval variant |
| `uuid`        | 2950          | Value has no UUID variant |
| `json`/`jsonb`| 114, 3802     | Value has no JSON variant |
| `array` types | various       | Value has no array/list variant |
| `point`, etc. | geometric     | Value has no geometric variant |
| `inet`/`cidr` | 869, 650      | Value has no network variant |

> These are known gaps, not bugs. When alfi core's Value adds new variant
> types (Phase 2+), chatter-pg should add OID cases for them.

---

## Transaction Implementation

Transactions use `execute()` internally via a private `executeSimple()` helper:

- `beginTransaction()` → `PQexec(conn_, "BEGIN")`
- `commit()` → `PQexec(conn_, "COMMIT")`
- `rollback()` → `PQexec(conn_, "ROLLBACK")`

We use `PQexec` (not `PQexecParams`) for these because:
1. They take no parameters.
2. `PQexec` is simpler and avoids the overhead of parameter marshaling.
3. The SQL is hardcoded, not user-supplied, so there's no injection risk.

All three throw `alfi::db::DbError` on failure.

### No Savepoints / Nested Transactions

The alfi core `Connection` interface has `beginTransaction()`, `commit()`,
`rollback()` — no `savepoint()`. We don't add savepoint support beyond
the interface contract. If needed, callers can use
`conn->execute("SAVEPOINT sp1")` directly.

---

## Interface Fitness Notes

### Connection Pooling

The alfi core `Driver::connect()` returns a single connection. There is no
connection pool abstraction in the interface. chatter-pg creates one
libpq connection per `connect()` call. Connection pooling would need to be
either:
- Added to alfi core's interface (e.g. a `Pool` class), or
- Handled externally (e.g. PgBouncer).

This is noted, not worked around.

### Prepared Statement Caching

`PQexecParams` prepares and executes in one step (unnamed/ephemeral
prepared statement). We do not cache named prepared statements. This is
simpler and correct; performance-sensitive callers should use PgBouncer
or a future prepared-statement layer.

### Async Queries

The alfi core `Connection::execute()` is synchronous (returns a result, not
a future). chatter-pg uses synchronous libpq calls accordingly. Async
support would require an interface change in alfi core.

---

## FetchContent vs. Other Dependency Mechanisms

We use CMake `FetchContent` for alfi core (header-only db/ interface) and
Catch2 (test framework). libpq is found via `pkg-config` since it's a
system-level C library that should be installed separately.

FetchContent was chosen because:
- It's built into CMake 3.14+ (no extra tools needed).
- It allows pinning to a specific commit/tag.
- The alfi core repo already uses FetchContent for its own deps.

Alternative approaches (git submodules, Conan, vcpkg) were considered but
not needed for this project's scope.

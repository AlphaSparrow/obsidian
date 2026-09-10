# 03 · Module Map

> Directory of all logical modules, their boundaries, and ownership.

## Module Definitions

| Module | Path / Package | Responsibility | Depends On | Owner |
|--------|---------------|----------------|-----------|-------|
| `auth` | | Authentication & authorization | `db`, `cache` | |
| `api` | | HTTP handler layer | `auth`, domain modules | |
| `core` | | Core domain logic | `db` | |
| `worker` | | Background job processing | `queue`, `core` | |
| `storage` | | File / asset management | `object-store` | |
| `notifications` | | Email / push delivery | `queue`, external APIs | |
| `config` | | App configuration loading | — | |
| `shared` | | Cross-cutting utilities | — | |

## Dependency Graph

```
api
 ├── auth
 ├── core
 │    └── db
 └── storage

worker
 ├── core
 └── queue

notifications
 └── queue
```

## Module Boundaries (Rules)

- Modules **must not** import from sibling modules unless explicitly listed in "Depends On."
- All cross-module communication happens via **interfaces / ports**, not concrete implementations.
- Shared utilities live in `shared/` — no business logic allowed there.

## Ownership Matrix

| Module | Primary Owner | Secondary Owner | On-call Rotation |
|--------|--------------|----------------|-----------------|
| `auth` | | | |
| `api` | | | |
| `core` | | | |
| `worker` | | | |

## Deprecated / Legacy Modules

| Module | Reason | Replacement | Sunset Date |
|--------|--------|-------------|------------|
| | | | |

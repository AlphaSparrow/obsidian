# 04 · Storage Design

> Schema, indexing strategy, and data retention policy for all storage layers.

## Storage Layer Overview

| Layer | Technology | Role | Managed / Self-hosted |
|-------|-----------|------|-----------------------|
| Primary DB | | Source of truth | |
| Cache | | Hot-path reads | |
| Object Store | | Binary / large assets | |
| Message Queue | | Async event delivery | |
| Search Index | | Full-text search | |

---

## Primary Database

### Schema Overview

```
Table: users
  id          UUID  PK
  email       TEXT  UNIQUE NOT NULL
  created_at  TIMESTAMPTZ
  updated_at  TIMESTAMPTZ

Table: <entity>
  id          UUID  PK
  user_id     UUID  FK → users.id
  ...
  created_at  TIMESTAMPTZ
```

### Indexes

| Table | Column(s) | Type | Reason |
|-------|-----------|------|--------|
| `users` | `email` | B-Tree | Login lookup |
| | | | |

### Migrations Strategy

- All schema changes via versioned migration files (e.g. Flyway / Alembic / Liquibase).
- Migrations must be **backward-compatible** — no destructive changes without a deprecation window.

---

## Cache

| Key Pattern | TTL | Eviction Policy | Invalidation Strategy |
|-------------|-----|----------------|----------------------|
| `user:{id}` | 5 min | LRU | On write to DB |
| | | | |

---

## Object Store

| Bucket / Container | Content | Lifecycle Rule | Access |
|-------------------|---------|---------------|--------|
| `assets-raw` | Uploaded files | Delete after 90 days | Private |
| `assets-processed` | Transformed outputs | Indefinite | CDN-public |

---

## Data Retention Policy

| Data Category | Retention Period | Deletion Method | Legal Basis |
|---------------|-----------------|----------------|------------|
| User PII | Until account deletion + 30 days | Hard delete | GDPR Art. 17 |
| Audit logs | 2 years | Archive → purge | Compliance |
| Analytics events | 1 year | Rolling delete | Business |

---

## Backup & Recovery

| Layer | Backup Frequency | Retention | RTO | RPO |
|-------|-----------------|-----------|-----|-----|
| Primary DB | Daily (full) + continuous WAL | 30 days | 4 h | 1 h |
| Object Store | Versioning enabled | 90 days | 1 h | 0 |

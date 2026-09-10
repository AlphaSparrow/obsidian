# 06 · API Contracts

> Canonical reference for all public and internal API surfaces.

## Conventions

- **Base URL**: `https://api.example.com/v1`
- **Auth**: Bearer token in `Authorization` header
- **Format**: JSON (UTF-8)
- **Dates**: ISO 8601 (`2024-01-15T10:30:00Z`)
- **Pagination**: Cursor-based — `?cursor=<token>&limit=<n>`
- **Errors**: RFC 7807 Problem Details

## Error Schema

```json
{
  "type": "https://api.example.com/errors/not-found",
  "title": "Resource Not Found",
  "status": 404,
  "detail": "User with id '123' does not exist.",
  "instance": "/users/123"
}
```

## Standard HTTP Status Codes

| Code | Meaning |
|------|---------|
| 200 | Success |
| 201 | Created |
| 204 | No content (delete success) |
| 400 | Bad request / validation error |
| 401 | Unauthenticated |
| 403 | Forbidden |
| 404 | Not found |
| 409 | Conflict |
| 422 | Unprocessable entity |
| 429 | Rate limited |
| 500 | Internal server error |

---

## Endpoints

### `POST /auth/token`

Issue an access token.

**Request**
```json
{
  "grant_type": "password",
  "email": "user@example.com",
  "password": "s3cr3t"
}
```

**Response `200`**
```json
{
  "access_token": "eyJ...",
  "token_type": "Bearer",
  "expires_in": 3600
}
```

---

### `GET /users/{id}`

Retrieve a user by ID.

**Path Params**

| Param | Type | Description |
|-------|------|-------------|
| `id` | UUID | User identifier |

**Response `200`**
```json
{
  "id": "550e8400-e29b-41d4-a716-446655440000",
  "email": "user@example.com",
  "created_at": "2024-01-15T10:30:00Z"
}
```

---

### `GET /<resource>`

List resources with optional filters.

**Query Params**

| Param | Type | Default | Description |
|-------|------|---------|-------------|
| `cursor` | string | — | Pagination cursor |
| `limit` | int | 20 | Items per page (max 100) |
| `filter[field]` | string | — | Field filter |

---

## Versioning Policy

- API version is in the URL path: `/v1/`, `/v2/`.
- Breaking changes require a new major version.
- Old versions supported for **12 months** after a new version ships.
- Deprecation notices sent via `Sunset` and `Deprecation` headers.

## Rate Limits

| Tier | Requests / min | Burst |
|------|---------------|-------|
| Free | 60 | 10 |
| Pro | 600 | 50 |
| Internal | Unlimited | — |

## Changelog

| Date | Version | Change |
|------|---------|--------|
| | v1 | Initial release |

# 02 · Data Flow

> End-to-end trace of how data moves through the system.

## Primary Request Lifecycle

```
Client
  │
  ├─► [1] Auth / API Gateway
  │         Validates token, applies rate-limit
  │
  ├─► [2] Core Service
  │         Validates input, runs business logic
  │         Reads from Cache → fallback to DB
  │
  ├─► [3] Database / Cache
  │         Persists or returns data
  │
  └─► [4] Response to Client
```

## Async / Event-Driven Flow

```
Core Service
  │
  └─► [1] Publishes event to Message Queue
              │
              └─► [2] Worker consumes event
                          │
                          ├─► [3a] External API call
                          └─► [3b] Writes result to DB
```

## Data Flow Diagram (Detailed)

| Step | Source | Destination | Data | Protocol | Auth |
|------|--------|-------------|------|----------|------|
| 1 | Client | API Gateway | Request payload | HTTPS | JWT |
| 2 | API Gateway | Core Service | Validated request | Internal HTTP | Service token |
| 3 | Core Service | Cache | Read query | Redis protocol | — |
| 4 | Core Service | Database | Read / Write | TCP | Credentials |
| 5 | Core Service | Message Queue | Event envelope | AMQP / SQS | IAM |
| 6 | Worker | External API | Outbound call | HTTPS | API Key |

## Sensitive Data Handling

| Data Type | Classification | At Rest | In Transit |
|-----------|---------------|---------|-----------|
| PII | Confidential | Encrypted (AES-256) | TLS 1.3 |
| Credentials | Secret | Secrets manager | TLS 1.3 |
| Logs | Internal | Redacted | TLS 1.3 |

## Known Bottlenecks

- [ ] Identify and document here as they surface.

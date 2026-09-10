# 05 · Runtime Design

> Deployment topology, scaling model, and operational concerns.

## Deployment Topology

```
                        ┌─────────────────────┐
                        │    Load Balancer     │
                        │  (TLS termination)   │
                        └──────────┬──────────┘
                    ┌──────────────┼──────────────┐
                    │              │              │
             ┌──────▼──────┐ ┌────▼────┐ ┌──────▼──────┐
             │  Service    │ │ Service │ │  Service    │
             │ Instance 1  │ │   N     │ │ Instance 2  │
             └─────────────┘ └─────────┘ └─────────────┘
                    │                          │
             ┌──────▼──────────────────────────▼──────┐
             │              Data Layer                  │
             │   (Primary DB · Replica · Cache · Queue) │
             └──────────────────────────────────────────┘
```

## Container / Process Model

| Service | Image / Runtime | Min Instances | Max Instances | CPU | Memory |
|---------|----------------|--------------|--------------|-----|--------|
| API | | 2 | 10 | 0.5 vCPU | 512 MB |
| Worker | | 1 | 5 | 1 vCPU | 1 GB |
| Scheduler | | 1 | 1 | 0.25 vCPU | 256 MB |

## Scaling Strategy

| Trigger | Action | Cooldown |
|---------|--------|---------|
| CPU > 70% for 2 min | +1 API instance | 3 min |
| Queue depth > 500 | +1 Worker | 1 min |
| CPU < 20% for 10 min | -1 instance | 5 min |

## Health Checks

| Endpoint | Type | Interval | Threshold |
|----------|------|----------|-----------|
| `GET /health/live` | Liveness | 10 s | 3 failures → restart |
| `GET /health/ready` | Readiness | 5 s | 2 failures → remove from LB |

## Configuration Management

| Config Type | Source | Rotation |
|-------------|--------|---------|
| App settings | Environment variables | On deploy |
| Secrets | Secrets Manager (Vault / AWS SM) | Automatic 30-day rotation |
| Feature flags | Remote config service | Real-time |

## Observability

| Signal | Tool | Retention |
|--------|------|-----------|
| Metrics | Prometheus / CloudWatch | 15 days |
| Logs | Loki / CloudWatch Logs | 30 days |
| Traces | Tempo / X-Ray | 7 days |
| Alerts | PagerDuty / OpsGenie | — |

## Incident Runbooks

- [ ] High error rate → `runbooks/high-error-rate.md`
- [ ] Database slow queries → `runbooks/db-slow-queries.md`
- [ ] Worker queue backlog → `runbooks/worker-backlog.md`

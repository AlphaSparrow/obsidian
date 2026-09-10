# 08 · Performance Budget

> Hard limits and targets for latency, throughput, resource consumption, and reliability.

## Why a Performance Budget?

A performance budget makes implicit expectations explicit. Any change that violates a budget **must** be flagged in review and requires a conscious decision to raise the limit.

---

## API Latency Targets

> Measured at the 50th, 95th, and 99th percentile under normal load.

| Endpoint / Operation | p50 | p95 | p99 | Hard Limit |
|---------------------|-----|-----|-----|-----------|
| `GET /health` | 5 ms | 10 ms | 20 ms | 50 ms |
| `POST /auth/token` | 80 ms | 200 ms | 400 ms | 1 s |
| `GET /<list>` | 50 ms | 150 ms | 300 ms | 1 s |
| `GET /<single>` | 20 ms | 80 ms | 200 ms | 500 ms |
| `POST /<create>` | 100 ms | 250 ms | 500 ms | 2 s |
| Background job (e2e) | — | — | — | 30 s |

---

## Throughput Targets

| Service | Steady-state RPS | Peak RPS | Design Capacity |
|---------|-----------------|---------|----------------|
| API | | | |
| Worker | jobs/s | jobs/s | |

---

## Availability & Reliability

| Metric | Target | Measurement Window |
|--------|--------|--------------------|
| Uptime (API) | 99.9% | Rolling 30 days |
| Error rate (5xx) | < 0.1% | Rolling 1 hour |
| Background job success rate | > 99.5% | Rolling 24 hours |
| Mean Time to Recovery (MTTR) | < 30 min | Per incident |

---

## Resource Consumption Limits

| Resource | Per-instance Limit | Alert Threshold |
|----------|-------------------|----------------|
| CPU | 80% sustained | 70% for > 5 min |
| Memory | 80% of allocation | 75% for > 5 min |
| DB connection pool | 80% utilized | 70% for > 2 min |
| Cache hit rate | > 80% | < 70% |
| Queue depth | < 1000 msgs | > 500 msgs |

---

## Frontend / Client Budgets (if applicable)

| Metric | Budget | Tool |
|--------|--------|------|
| Time to First Byte (TTFB) | < 200 ms | Lighthouse |
| Largest Contentful Paint (LCP) | < 2.5 s | Lighthouse |
| Total Blocking Time (TBT) | < 200 ms | Lighthouse |
| Cumulative Layout Shift (CLS) | < 0.1 | Lighthouse |
| JS bundle (initial) | < 200 KB gzip | Bundlesize |
| CSS bundle | < 50 KB gzip | Bundlesize |

---

## Load Test Scenarios

| Scenario | Duration | Users | Success Criteria |
|----------|---------|-------|-----------------|
| Baseline | 5 min | 50 VU | All targets met |
| Ramp-up | 10 min | 0 → 500 VU | p99 < hard limit |
| Soak | 30 min | 200 VU | No memory leak |
| Spike | 2 min | 0 → 1000 VU | < 1% errors |

**Tooling**: k6 / Locust / Artillery (choose one and link config).

---

## Budget Violation Protocol

1. **Detect** — CI load test or production alert fires.
2. **Triage** — On-call identifies root cause within 15 min.
3. **Decide** — Fix regression **or** explicitly raise the budget with team approval.
4. **Document** — Record decision and rationale in this file.

## Budget History

| Date | Metric | Old Value | New Value | Reason |
|------|--------|-----------|-----------|--------|
| | | | | Initial baseline |

# 01 · System Map

> High-level map of every major system component and how they relate.

## Overview

```
┌──────────────────────────────────────────────────────────┐
│                        Clients                           │
│            (Web App / Mobile App / CLI / API)            │
└────────────────────────┬─────────────────────────────────┘
                         │ HTTPS / WebSocket
┌────────────────────────▼─────────────────────────────────┐
│                      API Gateway                         │
│              (Auth · Rate Limit · Routing)               │
└──────────┬─────────────────────────────┬─────────────────┘
           │                             │
┌──────────▼──────────┐   ┌─────────────▼─────────────────┐
│   Core Service(s)   │   │       Background Workers       │
│                     │   │   (Queue consumers / Cron)     │
└──────────┬──────────┘   └─────────────┬─────────────────┘
           │                             │
┌──────────▼─────────────────────────────▼─────────────────┐
│                    Data Layer                             │
│         (Primary DB · Cache · Object Store · Queue)      │
└──────────────────────────────────────────────────────────┘
```

## Component Inventory

| Component | Technology | Responsibility | Owner |
|-----------|-----------|----------------|-------|
| API Gateway | | Auth, routing, rate-limiting | |
| Core Service | | Business logic | |
| Background Workers | | Async processing | |
| Primary Database | | Persistent state | |
| Cache | | Read performance | |
| Object Store | | Binary / large assets | |
| Message Queue | | Decoupled communication | |

## External Integrations

| System | Direction | Protocol | Purpose |
|--------|-----------|----------|---------|
| | Outbound | | |
| | Inbound | | |

## Environment Overview

| Environment | Purpose | URL / Endpoint |
|-------------|---------|---------------|
| Development | Local iteration | localhost |
| Staging | Pre-prod validation | |
| Production | Live traffic | |

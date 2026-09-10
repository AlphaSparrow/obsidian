# 07 · Plugin System

> Design of the extension / plugin architecture — how third-party or internal plugins integrate.

## Goals

- Allow functionality to be extended **without modifying core code**.
- Provide a stable, versioned **plugin API**.
- Guarantee plugins are **isolated** — a faulty plugin must not crash the host.

## Plugin Lifecycle

```
Discovery → Validation → Registration → Activation → Runtime → Deactivation → Cleanup
```

| Phase | Description |
|-------|-------------|
| **Discovery** | Host scans plugin directories / registry for manifests |
| **Validation** | Manifest schema check, signature verification |
| **Registration** | Plugin registers its hooks and capabilities |
| **Activation** | Plugin receives context; `onActivate()` called |
| **Runtime** | Plugin responds to events via registered hooks |
| **Deactivation** | `onDeactivate()` called; resources released |
| **Cleanup** | Temp files removed, metrics flushed |

## Plugin Manifest (`plugin.json`)

```json
{
  "id": "com.example.my-plugin",
  "name": "My Plugin",
  "version": "1.0.0",
  "apiVersion": "^2.0",
  "description": "Does something useful.",
  "author": "Example Corp",
  "main": "dist/index.js",
  "hooks": ["onRequest", "onResponse"],
  "permissions": ["read:data", "write:notifications"],
  "config": {
    "schema": "config.schema.json"
  }
}
```

## Hook System

| Hook | Trigger | Payload | Can Mutate? |
|------|---------|---------|-------------|
| `onRequest` | Incoming HTTP request | Request object | ✅ |
| `onResponse` | Outgoing HTTP response | Response object | ✅ |
| `onEvent` | Domain event emitted | Event envelope | ❌ |
| `onSchedule` | Cron trigger | Schedule context | ❌ |
| `onActivate` | Plugin startup | Host context | ❌ |
| `onDeactivate` | Plugin shutdown | — | ❌ |

## Plugin API Surface

Plugins receive a host context object with controlled access:

```typescript
interface PluginContext {
  // Logging (scoped to plugin id)
  logger: Logger;
  // Key-value store (isolated namespace)
  store: PluginStore;
  // Emit domain events
  emit(event: string, payload: unknown): Promise<void>;
  // Register a cron job
  schedule(cron: string, handler: () => Promise<void>): void;
  // HTTP client (outbound only)
  http: HttpClient;
}
```

## Isolation Model

| Concern | Strategy |
|---------|---------|
| CPU limits | Plugin runs in a worker thread / subprocess |
| Memory limits | OS-level limits per process |
| Crash isolation | Host catches uncaught errors; plugin is disabled |
| Filesystem | Plugins only access their own sandbox directory |
| Network | Outbound-only via controlled `http` client |

## Permissions Model

Plugins must declare required permissions in their manifest. Undeclared permission calls throw at runtime.

| Permission | Description |
|-----------|-------------|
| `read:data` | Read domain entities |
| `write:data` | Mutate domain entities |
| `read:config` | Access app configuration |
| `write:notifications` | Send notifications |
| `external:http` | Make outbound HTTP calls |

## Plugin Registry

| Plugin ID | Version | Status | Maintainer | Description |
|-----------|---------|--------|-----------|-------------|
| | | | | |

## Plugin Development Guide

1. Implement the `Plugin` interface.
2. Create a `plugin.json` manifest.
3. Run `plugin-sdk validate` to lint your manifest.
4. Submit a PR to the plugin registry or deploy to your own registry.

# CCF Deep Dive: Request Flow & Application Model

## Pillar 1: Request Lifecycle

**Key file:** `doc/architecture/request_flow.rst`

The doc has mermaid sequence diagrams ready for slides. Three flows:

### Normal flow (primary node)

```
Client → TLS decrypt → HTTP parse → Frontend Dispatch → App endpoint
  → KV read/write → tx.commit() → serialize response → TLS encrypt → Client
```

Key points:
- A **`kv::Tx` is created** at the start of frontend dispatch and **destroyed** after commit
- The frontend calls `is_open()` to verify the service is accepting requests
- `find_endpoint()` looks up the handler in the app's endpoint registry
- `get_authenticated_identity()` runs the endpoint's auth policies
- The frontend checks whether to **forward** based on endpoint metadata
- After the app handler returns, `tx.commit()` is called automatically
- The **commit TX ID** is added as a response header (`x-ms-ccf-transaction-id`)
- Response status determines commit: **2xx → apply**, anything else → discard (overridable with `set_apply_writes(true)`)

### Forwarding flow (backup → primary)

```
Client → Backup A [TLS decrypt, HTTP parse, endpoint lookup, auth check]
  → determines "forward needed"
  → queues N2N message to Primary B
  → keeps TLS session open, marks pending

Primary B [receives forwarded cmd, runs full dispatch + app execution + commit]
  → sends response back over N2N to A

Backup A → writes response to original TLS session → Client
```

The backup does enough parsing to identify the endpoint and determine forwarding is needed, but does **not** execute the handler.

Forwarding behaviour is controlled per-endpoint via `.set_forwarding_required()`:
- **`ForwardingRequired::Always`** (default for `make_endpoint`) — writes must go to primary; backup always forwards
- **`ForwardingRequired::Sometimes`** (default for `make_read_only_endpoint`) — forwards only if the session was already forwarded, to maintain session consistency
- **`ForwardingRequired::Never`** — always execute locally (e.g. historical queries, node-local state). If this tries to write on a backup, it will fail

### Redirection flow (alternative to forwarding)

Instead of transparently forwarding over the N2N channel, the node can return an HTTP redirect:
- Returns **HTTP 307** with `Location` header pointing to the primary
- Two modes, configured per-endpoint via `RedirectionStrategy`:
  - **`ToPrimary`** — redirect directly to primary node (requires nodes to have publicly accessible names)
  - **`ToBackup`** — redirect to a backup (for read-heavy load balancing)
  - **`None`** (default) — no redirection, execute locally or forward
- For deployments where nodes aren't directly accessible, redirections can go via a **write load balancer** (e.g. `write.service.ccf.com`)
- Warning: many HTTP clients strip `Authorization` headers on cross-origin redirects — intercept redirects manually if using JWT auth

### Threading model

From `doc/architecture/threading.rst` — just one key slide:
- Multiple worker threads for throughput
- **Session consistency guarantee**: all commands from the same TLS connection execute on the same thread, in order
- Do NOT mutate global state outside the KV — all inter-command communication must go through the store

---

## Pillar 2: Application Model

Two runtime options: **C++ endpoint registries** or **JavaScript bundles**.

### C++ Pattern

Source: `samples/apps/logging/logging.cpp`, `samples/apps/logging/logging_schema.h`

**1. Define your tables:**

```cpp
using RecordsMap = ccf::kv::Map<size_t, string>;
static constexpr auto PUBLIC_RECORDS = "public:records";
static constexpr auto PRIVATE_RECORDS = "records";
```

**2. Subclass `ccf::UserEndpointRegistry`:**

```cpp
class LoggerHandlers : public ccf::UserEndpointRegistry { ... };
```

**3. Write a handler and register it in `init_handlers()`:**

```cpp
auto record = [](auto& ctx, nlohmann::json&& params) {
  const auto in = params.get<LoggingRecord::In>();
  auto records_handle = ctx.tx.template rw<RecordsMap>("records");
  records_handle->put(in.id, in.msg);
  return ccf::make_success(true);
};

make_endpoint("/log/private", HTTP_POST, ccf::json_adapter(record), auth_policies)
  .install();
```

Key endpoint types:
- `make_endpoint()` — read-write (requires primary)
- `make_read_only_endpoint()` — reads only (can run on any node)
- `make_command_endpoint()` — no KV access

**Layers of read/write enforcement** — there are three separate levels, which is worth calling out:
1. **Endpoint declaration** (`make_read_only_endpoint` vs `make_endpoint`): gives you compile-time type safety — a read-only endpoint's handler receives a `ReadOnlyTx`, so calling `put()` won't compile. This is a developer-time guardrail.
2. **Primary-only writes**: independently, the framework enforces that write transactions can only be committed on a primary node. If a write somehow reaches a backup (e.g. `ForwardingRequired::Never` endpoint that tries to write), it will fail at commit time. This is a runtime guardrail.
3. **Governance/application namespace restrictions**: separately again, the KV enforces access control based on table namespace and execution context (covered in Pillar 3). This is relevant to JS apps especially, where the runtime enforces which tables you can read/write.

**4. Auth policies** — the 4th argument to `make_endpoint()` is a list of authentication policies. They're tried in order; the first one that accepts wins:

```cpp
const ccf::AuthnPolicies auth_policies = {
  ccf::jwt_auth_policy,
  ccf::user_cert_auth_policy,
  ccf::user_cose_sign1_auth_policy
};

// Applied directly in the endpoint registration:
make_endpoint("/log/private", HTTP_POST, handler, auth_policies)
  .install();
```

Custom policies can also be implemented — the logging sample includes a `CustomAuthPolicy` that checks custom headers.

**5. OpenAPI schema (optional):** Two approaches for generating API documentation:
- **Auto-generation:** Define your structs with `DECLARE_JSON_TYPE` / `DECLARE_JSON_REQUIRED_FIELDS` macros, then chain `.set_auto_schema<RequestType, ResponseType>()` on the endpoint. CCF derives the OpenAPI schema from the C++ types. Use `<void, T>` for endpoints with no request body.
- **Manual control:** Override `build_api(nlohmann::json& document, kv::ReadOnlyTx& tx)` on your endpoint registry to construct your own OpenAPI document directly, if the auto-generators don't fit your needs.

### JavaScript Pattern

Source: `samples/apps/logging/js/src/logging.js`

```javascript
export function get_private(request) {
  const parsedQuery = parse_request_query(request);
  const id = get_id_from_query(parsedQuery);
  const msg = ccf.kv["records"].get(id);
  if (msg === undefined) {
    return { statusCode: 404, body: { error: { code: "ResourceNotFound" } } };
  }
  return { body: { msg: ccf.bufToStr(msg) } };
}
```

Key differences from C++:
- **Raw byte access to KV:** Under the hood, the KV stores bytearray-to-bytearray. In C++, the typed `kv::Map<K, V>` wrappers handle serialization transparently. In JS, you work with `ArrayBuffer` keys and values directly via `ccf.kv["map_name"]`, and explicitly call `ccf.strToBuf()` / `ccf.bufToStr()` (or other conversions) yourself. TypeScript wrappers from the `@microsoft/ccf-app` package (`js/ccf-app/src/global.ts`, `js/ccf-app/src/endpoints.ts`) can provide some type enforcement on top of this.
- Endpoint registration via `app.json` manifest rather than code
- Return `{ statusCode, body }` objects

---

## Pillar 3: KV Store

**Key file:** `doc/build_apps/kv/kv_how_to.rst`

### Core concepts

| Concept | Detail |
|---------|--------|
| **Map** | Named collection of typed key-value pairs. Created implicitly on first write. |
| **Public vs Private** | `"public:foo"` → plaintext on ledger (integrity-protected). `"foo"` → encrypted on ledger. |
| **Transaction (`kv::Tx`)** | Atomic unit of interaction. Each endpoint handler gets one. Handles conflicts via automatic re-execution. |
| **MapHandle** | Obtained via `tx.rw<MapType>("name")`. Supports `get()`, `put()`, `remove()`, `has()`, `foreach()`. |
| **Read/Write-only handles** | `tx.ro()` and `tx.wo()` — compile-time safety for read-only or write-only access. |
| **Global commit** | A tx is globally committed when a majority of nodes have acknowledged it. `get_globally_committed()` queries this durable state. |

### Commit semantics

- 2xx response → transaction is applied to the KV and replicated
- Non-2xx → transaction is discarded (unless `set_apply_writes(true)` overrides)
- Committed TX ID returned via `x-ms-ccf-transaction-id` header

### Read/Write Restrictions

Source: `doc/audit/read_write_restrictions.rst`

Three table namespaces:

| Namespace | Prefix | Purpose |
|-----------|--------|---------|
| **Governance** | `public:ccf.gov.*` | Member-controlled, constitution-gated |
| **Internal** | `ccf.internal.*` | Framework-only (signatures, consensus) |
| **Application** | Anything else | Your app's data |

**Permission matrix** (the money slide):

| Context | Governance (public) | Application (public + private) |
|---------|-----------|-------------|
| Pre-approval governance (ballots, validate) | Read-only | None |
| Post-approval governance (apply) | Read/Write | **Write-only** |
| Application endpoints | **Read-only** | Read/Write |

Key insight: **App code CAN read governance tables** (for auth/metadata), but **CANNOT write** them. This ensures all governance changes are traceable to member signatures.

### Notable built-in tables (`public:ccf.gov.*`)

Source: `doc/audit/builtin_maps.rst`

- `members.certs` / `members.info` — consortium membership and status
- `users.certs` / `users.info` — registered users
- `nodes.info` — node identity, status, attestations
- `service.info` — service identity and status (Opening → Open → Recovering)
- `proposals` / `proposals_info` — governance proposals and voting state

---

## Suggested slide structure (~25 min)

1. **Title/Overview** — "How a request becomes a committed transaction" (1 min)
2. **Request lifecycle diagram** — the normal flow mermaid diagram, walk through each layer (5 min)
3. **Forwarding & Redirection** — one diagram each, emphasize primary/backup distinction (3 min)
4. **Building a C++ app** — walk through the logging sample: table → schema → handler → endpoint registration (5 min)
5. **JS alternative** — side-by-side comparison, highlight `ccf.kv` API (3 min)
6. **KV deep dive** — transactions, handles, public vs private, commit semantics (5 min)
7. **Access control matrix** — the permission table, governance isolation story (3 min)
8. **Q&A / Further reading** — point to threading, N2N channel docs, builtin maps (remaining time)

## Source files referenced

- `doc/architecture/request_flow.rst` — mermaid sequence diagrams for all request flows
- `doc/architecture/threading.rst` — threading model and session consistency
- `doc/architecture/node_to_node.rst` — N2N channel establishment and crypto
- `doc/build_apps/kv/kv_how_to.rst` — KV store API and transaction semantics
- `doc/build_apps/auth/index.rst` — authentication policies overview
- `doc/audit/read_write_restrictions.rst` — table namespace permissions
- `doc/audit/builtin_maps.rst` — governance and system table reference
- `samples/apps/logging/logging.cpp` — reference C++ application
- `samples/apps/logging/logging_schema.h` — JSON schema macros example
- `samples/apps/logging/js/src/logging.js` — reference JS application
- `js/ccf-app/src/global.ts` — TypeScript KV and CCF global API definitions
- `js/ccf-app/src/endpoints.ts` — TypeScript request/response types

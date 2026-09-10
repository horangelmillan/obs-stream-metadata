# Privacy Policy — obs-stream-metadata (T-060)

> This document describes what the software **actually does** (code at
> `master`, backend in production). It is not legal advice and makes no
> compliance certifications (no GDPR/CCPA/LGPD, SOC 2 or ISO claims).
> If a claim here cannot be traced to code/docs, treat the code as
> authoritative and report the mismatch.

## 1. Scope

`obs-stream-metadata` is an OBS Studio plugin plus a backend service.
It edits stream **title/description metadata** on Twitch, YouTube and
Kick. It has two connection modes (ADR-012): **Independent**
(direct/BYO-app OAuth from the plugin) and **Managed** (OAuth via the
production backend). Twitch Managed is unsupported.

## 2. Data We Process

### 2.1 Account and identity data

Per connected account, the minimum the provider APIs return:

| Provider | Identity fields read | Source |
|---|---|---|
| YouTube | channel/user id, display (channel title), granted scopes | `fetch_identity` (channels API) |
| Kick | `broadcaster_user_id`, `slug` (display), granted scopes | `fetch_identity` (channels API) |
| Twitch | `user_id`, `login` (display), granted scopes (validated) | `/validate` (device flow) |

Not collected: email, real name, avatar, location, or any profile
field the code never reads (verified in the three adapters).

### 2.2 OAuth and connection data

- OAuth `state` (single-use, TTL), PKCE verifiers (transient, one
  attempt), device/user codes (Twitch DCF, transient).
- Backend sessions: opaque `session_token` + `installation_id` +
  `issued_at`/`expires_at` (30 min TTL, `SESSION_TTL_S=1800`).
- Installation identity: `installation_id` (non-secret UUID) +
  `installation_secret` (HMAC signing only).
- OAuth transactions: `{id, provider, installation_id, state,
  code_verifier, redirect_uri, created_at, expires_at, consumed}`
  (TTL 600 s, single-use).
- HMAC nonces: in-memory only, 10 min window, lost on restart.

### 2.3 Stream metadata

Titles/descriptions are **transit-only**: typed in the dock, sent to
the provider on Apply, never persisted by any backend store
(`MetadataUpdate` docstring, `kernel.py`). The plugin keeps them in
UI memory only.

### 2.4 Technical/session data

Timestamps (`issued_at`, `expires_at`, `obtained_at`, `created_at`),
HTTP status codes, request IDs, rate-limit counters. No fingerprinting,
no analytics, no tracking.

### 2.5 Logs

Logged: event types, platform/mode names, HTTP codes, redacted lengths,
request IDs. Deliberately redacted: secrets, tokens, codes, verifiers,
cookies, `DATABASE_URL` passwords (`://user:***@`). Verified: no
display names, user IDs, tokens or titles in any log call
(`backend/logging_setup.py`, `RedactingFilter`, leak-guard tests;
plugin `obs_log` lines carry platform/mode/codes only).

## 3. Where Data Is Stored

### Local/plugin storage (DPAPI file, OBS module config dir, per-user)

`secure::Data` (`src/secure_store.h`):

| Record | Plaintext | DPAPI-encrypted blob |
|---|---|---|
| `twitch/youtube/kick` | `display`, `broadcaster` (Twitch user_id), `connected` | `access`, `refresh`, `clientId`, `secret` |
| `backendInstall` | `connected` | `clientId` (= installation_id), `secret` (= installation_secret) |
| `connectionMode`, `backendBaseUrl` | yes (context, not secrets) | — |
| `managedYoutube/managedKick` | `connected`, `user_id`, `display` | — (structurally secret-free, T-051) |

### Backend/PostgreSQL (production)

Migrations in `backend/migrations/` (reproducible schema):

| Table | User-related content |
|---|---|
| `installations` | `id`, `created_at`, `revoked`, installation `secret` |
| `sessions` | session payload (`token`, `installation_id`, issued/expiry) |
| `transactions` | OAuth transaction incl. `code_verifier`, `state`, redirect |
| `connections` | `(installation_id, provider)` → `{account: {provider_user_id, display_name, scopes}, obtained_at}` |
| `tokens` | `(provider, provider_user_id)` → `access_token`, `refresh_token`, `expires_in`, `scope` |

Provider secrets (`GOOGLE_/KICK_CLIENT_ID/SECRET`) live in Google
Secret Manager as files consumed via `FileSecretStore`/
`CompositeSecretStore`; `DATABASE_URL` via Secret Manager env binding.
Never in repo, images, or logs.

### Provider services

Tokens intentionally shared with the provider that issued them
(required for API calls). No other third parties receive user data.

## 4. Why Data Is Processed

Sole purpose: connect streaming accounts and update their public
stream title/description on user action. No secondary uses.

## 5. OAuth and Third-Party Providers

- **YouTube** (Independent + Managed): scopes `youtube.force-ssl`
  only. Managed flow via production backend + Cloud Run callback.
- **Kick** (Independent + Managed): scopes `channel:write`,
  `channel:read`. Exact redirect match required.
- **Twitch** (Independent only): Device Code Flow, no client secret
  distributed; public distributed Client ID possible via build flag.
  Managed explicitly unsupported — no Twitch data ever flows to the
  backend.

## 6. Managed Connections

A Managed connection = backend row (`connections` + `tokens`) +
local snapshot (`ManagedSnapshot{connected, user_id, display}`).
The snapshot **is not** a credential store:

```text
ManagedConn snapshot  ≠  backend TokenStore
{id, display labels}     {access/refresh tokens, secrets}
```

Revalidation uses `GET /connect/<provider>/status` (identity labels
only, never tokens). `Account` (Independent credentials) and
`ManagedConn` are separate structs end to end.

## 7. Tokens and Credentials

- Independent: user-supplied app credentials + user tokens in DPAPI
  (Twitch DCF needs no secret at all).
- Managed: provider client secrets only in backend Secret Manager;
  user access/refresh tokens only in backend `tokens` table; plugin
  holds neither (verified live: API returns `{provider, status,
  account}` only).
- Installation/session secrets: DPAPI (plugin) / PostgreSQL (backend).

## 8. Retention

Real behavior (no invented periods):

- Sessions: 30 min TTL, enforced on access; revocation **flags**
  the row (`revoked=true`) rather than deleting it, and expired rows
  are rejected but not purged — no background eraser is implemented.
  Rows are replaced when a new session is issued under the same token
  id only if the flow overwrites them; otherwise they persist.
- OAuth transactions/nonces: TTL 600 s / 10 min window; consumed or
  expired entries are dropped on access; nonces are memory-only.
- Connections + tokens: persist **until Disconnect** (no automatic
  expiry cleanup implemented).
- DPAPI file: persists until Disconnect (records rewritten without
  disconnected entries) — no time-based deletion.
- Logs: Cloud Run stdout retention is the platform default (operator
  configurable, not app code).

## 9. Revocation and Deletion

- Backend `disconnect`: **always** deletes the `connections` row and
  the `tokens` row; remote provider revoke is **best-effort**
  (failures swallowed after local deletion, F-030). Verify
  provider-side if in doubt.
- Plugin Disconnect: wipes local state first, then best-effort remote
  revoke (HTTP status logged, never tokens). Twitch may cache the
  authorization entry in its UI after a successful (HTTP 200) revoke.
- No "delete everything immediately" guarantee beyond the above; no
  cross-provider or cross-installation deletion (strict isolation).

## 10. Uninstallation

The Windows uninstaller removes **only** the plugin payload
(`obs-plugins/64bit/obs-stream-metadata.dll`,
`data/obs-plugins/obs-stream-metadata/`, registry keys). It does
**not** delete:

- the DPAPI config file (`%APPDATA%` untouched),
- backend PostgreSQL rows (installation, connections, tokens),
- provider-side authorizations (revoke via Disconnect first).

To fully leave: Disconnect each account (revokes + deletes), then
uninstall. Backend rows of an abandoned installation persist until
explicitly revoked/disconnected (limitation, not a silent purge).

## 11. Security Measures

DPAPI (CurrentUser) for local secrets; HMAC-signed short sessions;
OAuth state/PKCE/transactions single-use + TTL; log redaction +
leak-guard tests; provider/mode isolation (no silent fallback);
production fail-fast gates (no dev stores/secrets/HTTP in prod);
Secret Manager file mounts; per-installation identities. See
`docs/SECURITY.md` and ADRs 008–014.

## 12. Data Sharing / Third Parties

Data goes to: (1) the OAuth/API provider the user connects to, as
required for login and metadata updates; (2) the project's own
production backend + PostgreSQL (Managed). No analytics, ads,
brokers, or other recipients.

## 13. Incident Handling

No formal incident-response process is implemented in the product.
Project policy (not an implemented capability): credential or data
exposure is treated by revoking the affected secrets/tokens
(provider dashboards, Secret Manager rotation, Disconnect/revoke),
rotating `DATABASE_URL` credentials if the database was in scope,
and documenting the incident in `docs/FINDINGS.md`. No SLA or legal
timelines are promised.

## 14. User Requests / Contact

No dedicated privacy contact channel exists yet (**to be completed
by the operator**; do not use public issue trackers for sensitive
reports). User controls available today: per-provider Disconnect
(deletes local + backend rows, best-effort remote revoke), OBS
restart persistence semantics (§8–§9), uninstall scope (§10).

## 15. Changes to This Policy

Updated alongside behavior changes that affect data. The code is
authoritative over this document; mismatches should be reported and
fixed here, never by silently changing the system (T-060 rule).

## Appendix — Technical inventory

| Data | Origin | Location | Purpose | Retention | Deletion |
|---|---|---|---|---|---|
| `display`, `broadcaster` | provider APIs | DPAPI file (plaintext) | "Connected as" label, Twitch id | until Disconnect | Disconnect rewrites file |
| `access/refresh/clientId/secret` (Independent) | OAuth flows | DPAPI file (encrypted) | API calls | until Disconnect | Disconnect wipes |
| installation id+secret | bootstrap | DPAPI (plugin) + PG `installations` | backend auth | until revoke/uninstall-independent | revoke/re-bootstrap |
| `managedYoutube/managedKick` | `/status` | DPAPI file (plaintext) | reconnect labels | until Disconnect | snapshot omitted |
| `connectionMode`, `backendBaseUrl` | local/backend | DPAPI file (plaintext) | context/binding | until overwrite | — |
| session token record | `/auth/*` | PG `sessions` | bearer auth | 30 min TTL; **flagged, not deleted**, on revoke | new session / operator DB delete |
| OAuth transaction | `/connect/*` | PG `transactions` | CSRF/PKCE/exchange | 600 s TTL, single-use | consume/expiry |
| connection entry | callback | PG `connections` | status/reconnect | until Disconnect | disconnect |
| user tokens | exchange/refresh | PG `tokens` | API calls | until Disconnect | disconnect |
| provider client secrets | operator Secret Manager | Secret Manager → files | OAuth | managed externally | operator rotation |
| `DATABASE_URL` | operator Secret Manager | Secret Manager env | DB access | managed externally | operator rotation |
| title/description text | user typing | memory only (+ provider on Apply) | metadata update | transient | — |
| request logs | runtime | stdout/Cloud Logging | diagnostics | platform default | operator config |

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
| Facebook | user id, `name` (display), granted scopes (`publish_video`; `pages_*` diferidas F-072/F-079) | `fetch_identity` (`/me?fields=id,name`; ADR-015) |

Not collected: email, real name, avatar, location, or any profile
field the code never reads (verified in the three adapters).

Managed user accounts (v1, commit `0f015a3`) [C: code read in this
session; D: spec §3/§6, 2026-10-26]. `user_id` is the first
person-identifier of the Managed backend:

| Record | Identity fields stored | Source |
|---|---|---|
| `users` | `id` (opaque 128-bit `user_id`), `username` (3–32 chars, `[A-Za-z0-9._-]`, unique, case-sensitive), `password_hash` (`pbkdf2-sha256$iter$salt_b64$hash_b64`, 600 000 iterations, 16 B random salt; never plaintext), `created_at`/`updated_at` (epoch UTC); **no email in v1 ⇒ no verification nor recovery** | `POST /users/signup` (`backend/users.py`, `backend/migrations/003_users.sql`) |
| scope keys | `installations.user_id`, `sessions.user_id`, `connections.user_id`, `tokens.owner_user_id` (nullable until first login/signup adoption; a token belongs to the user, not the installation) | migration `003_users.sql`; adoption via `adopt_installation` |

Reserved name-only columns `totp_secret` / `oauth_provider` /
`oauth_subject` stay NULL in v1 (2FA/TOTP and OAuth-login out of scope;
no code activates them) [C]. Adoption of pre-existing anonymous
installations happens at first login/signup without regenerating
identity and without wiping `backendInstall` or connections/tokens [C:
`adopt_installation`, `installation_id_for_adoption`].

### 2.2 OAuth and connection data

- OAuth `state` (single-use, TTL), PKCE verifiers (transient, one
  attempt), device/user codes (Twitch DCF, transient).
- Backend sessions: opaque `session_token` + `installation_id` +
  `issued_at`/`expires_at` (30 min TTL, `SESSION_TTL_S=1800`).
- Installation identity: `installation_id` (non-secret UUID) +
  `installation_secret` (HMAC signing only).
- User sessions (Managed v1) [C: `backend/http_server.py`,
  `src/backend_auth.cpp`]: opaque `user_session` token sent as the
  `X-User-Session` header, 30 min TTL (`USER_SESSION_TTL_S=1800`),
  revocable (`POST /users/logout`, idempotent 200); validated against
  the `users` row on every use. Decisión del usuario 2026-10-26 [D:
  sustituye spec §5 memoria-solo]: the plugin persists it encrypted
  in the DPAPI file (`managedUser`: token+expiry+username,
  fail-closed on `backendBaseUrl` mismatch) until explicit logout /
  401 / expiry; opening OBS with a valid session goes straight to
  the panel, login only after expiry or logout [C:
  `src/secure_store.cpp`, `src/backend_auth.cpp` en este checkout].
- Dual auth on Managed connect/metadata routes [C:
  `check_managed_access`, `MANAGED_AUTH_DETAIL`]: a valid installation
  session (T-044) **plus** a valid user session are required on
  `/connect/*`, `/metadata/*`, `/connect/*/status` and
  `/users/erase`; any missing/invalid/expired/revoked side → normalized
  401 (`managed auth required`) without revealing which side failed.
- Login/signup rate-limit counters (10/10 min per IP + 5/10 min per
  username on login; 10/10 min per IP on signup) [C: `http_server.py`
  limiters + `backend/users.py` constants].
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
display names, provider user IDs, tokens or titles in any log call
(`backend/logging_setup.py`, `RedactingFilter`, leak-guard tests;
plugin `obs_log` lines carry platform/mode/codes only).
Managed v1 exception by design [D: spec §4; C: code `0f015a3`]:
account-audit lines log the opaque internal `user_id` plus counts only
(`users signup/login ok user_id=%s adopted=...`, `users erase ok
erased=<counts>`) — never username, password, hash, token or secret
(F-100).

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
| `installations` | `id`, `created_at`, `revoked`, installation `secret` | deleted entirely by erase (row DELETE, not just flag) |
| `sessions` | session payload (`token`, `installation_id`, issued/expiry) | deleted entirely by erase (`delete_for_installation`) |
| `transactions` | OAuth transaction incl. `code_verifier`, `state`, redirect | deleted entirely by erase (`delete_for_installation`) |
| `connections` | `(installation_id, provider)` → `{account: {provider_user_id, display_name, scopes}, obtained_at}` | deleted entirely by erase (all providers) |
| `tokens` | `(provider, provider_user_id)` → `access_token`, `refresh_token` (cifrados `v1:` Fernet, F-C1; legacy en claro solo hasta el backfill), `expires_in`, `scope` (en claro) | deleted by erase **unless another installation still references the same account** (shared `(provider, provider_user_id)` row is kept; remote revoke is still attempted) |
| `users` (Managed v1) | `id`, `username` (unique), `password_hash` (PBKDF2-SHA256 600k, never plaintext), `created_at`/`updated_at`; reserved NULL `totp_secret`/`oauth_provider`/`oauth_subject` (unused in v1); **no email stored** | deleted entirely by user erase (`POST /users/erase` cascade); never touched by installation erase (F-C2 intact) |

`user_id` (`installations`, `sessions`, `connections`) and
`owner_user_id` (`tokens`) scope every Managed row to its user after
adoption (nullable beforehand) [C: `003_users.sql`; `adopt_installation`].
`POST /users/erase` deletes the user row plus all of their installations,
sessions, connections and unshared token rows in cascade (local first,
best-effort provider revoke afterwards, idempotent, counts only), and is
a different operation from `POST /privacy/erase` (F-C2, per-installation
wipe that never touches the `users` table — `backend/privacy.py` untouched
in `0f015a3` [C: commit file list]).

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
Managed user accounts (v1) exist solely to authenticate that access
(login identity for Managed connect/apply/erase); they add no profile,
no email, no tracking and no secondary use [C: `0f015a3` stores only the
§2.1b fields; D: spec §§4/6].

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
Managed actions additionally require a logged-in user (dock gate per
spec §5 [D]; the gate predicates are covered by selfcheck f4 [C]).
Erasing a user account (`POST /users/erase`) is a separate operation
from the installation erase in §9 (F-C2): it removes the user and all of
their installations, never just one installation [C: `erase_user`;
D: spec §5].

## 7. Tokens and Credentials

- Independent: user-supplied app credentials + user tokens in DPAPI
  (Twitch DCF needs no secret at all).
- Managed: provider client secrets only in backend Secret Manager;
  user access/refresh tokens only in backend `tokens` table; plugin
  holds neither (verified live: API returns `{provider, status,
  account}` only).
  The Managed user-login session is an opaque token persisted
  encrypted in the DPAPI file (`managedUser`, CurrentUser scope;
  decisión usuario 2026-10-26 [D], sustituye el memoria-solo de
  spec §5), never in plaintext; the backend validates it against
  `users` on every Managed call (backend TTL governs: file presence
  ≠ session validity).
- Installation/session secrets: DPAPI (plugin) / PostgreSQL (backend).

## 8. Retention

Real behavior (no invented periods):

- Sessions: 30 min TTL, enforced on access; revocation **flags**
  the row (`revoked=true`) rather than deleting it, and expired rows
  are rejected on access. A daily purge job (`POST /ops/purge` via
  Cloud Scheduler, F-C4) physically deletes sessions that are expired
  or revoked — effective retention ≤ ~25 h after issue/revocation.
  Rows are replaced when a new session is issued under the same token
  id only if the flow overwrites them; otherwise they persist until
  the purge. Erase (`POST /privacy/erase`, F-C2) deletes **all**
  sessions of the installation outright.
- OAuth transactions/nonces: TTL 600 s / 10 min window; consumed or
  expired entries are dropped on access; nonces are memory-only. The
  same daily purge deletes transactions that are expired or consumed
  (including `code_verifier`s) — effective retention ≤ ~24 h after
  expiry/use. Erase deletes pending transactions of the installation
  outright.
- Connections + tokens: persist **until Disconnect** (no automatic
  expiry cleanup implemented). Erase deletes all connections of the
  installation plus unshared token rows (shared rows are kept while
  another installation references the same account, but remote revoke
  is still attempted).
- Installation row: persists until Erase, which DELETEs it (the local
  installation secret becomes useless; reconnecting provisions a fresh
  installation via bootstrap).
- `users` rows (Managed v1): no automatic expiry is implemented; a user
  row (id, username, PBKDF2 hash, timestamps) persists until account
  erase (`POST /users/erase`), which DELETEs it [C: no TTL in
  `backend/users.py` / `003_users.sql`; deletion via `users.delete` in
  `erase_user`]. **v1 has no email, hence no verification nor password
  recovery** — accepted consequence, stated on the signup page itself
  [C: `signup_page` notice; D: spec §7].
- User sessions: 30 min access TTL like installation sessions (same
  sessions store by default; revoked/expired rejected on access); the
  daily F-C4 purge physically deletes expired/revoked rows [C].
  Whether a separately configured (non-default) `user_sessions` store
  is covered by the purge is [P].
- Account audit: signup/login/logout/erase events log only the opaque
  `user_id` plus counts — never username, password, hash, token or
  secret [C: `signup_user` / `login_user` / `erase_user` log calls];
  audit retention window [P] (spec §11).
- DPAPI file: persists until Disconnect (records rewritten without
  disconnected entries) — no time-based deletion. Erase clears the
  Managed snapshots + local `backendInstall` record; Independent
  records are untouched.
- Plugin `managedUser` session record: DPAPI-encrypted (CurrentUser)
  `token`+`expiry`+`username`, bound to `backendBaseUrl` (fail-closed
  mismatch → treated as logged-out); persists until explicit logout,
  backend 401, or 30 min expiry — never in plaintext [D: decisión
  usuario 2026-10-26; C: `src/secure_store.cpp`/`backend_auth.cpp` en
  este checkout]. Backend TTL governs: the session expires even if the
  file still exists.
- Logs: Cloud Run stdout retention is the platform default (operator
  configurable, not app code). Log lines carry event types,
  platform/mode names, HTTP codes, redacted lengths, request IDs and
  erase counts only — never tokens, secrets, user IDs, display names
  or titles.

## 9. Revocation and Deletion

- Backend `disconnect`: **always** deletes the `connections` row and
  the `tokens` row (unless another installation shares the same
  account row, F-C2 guarded-delete); remote provider revoke is
  **best-effort** (failures swallowed after local deletion, F-030).
  Verify provider-side if in doubt.
- Backend `POST /users/erase` (Managed v1 account erase, requires dual
  auth): deletes the user row + **all** of their installations, sessions,
  connections and unshared token rows in cascade (a shared row is kept
  while another connection references it, and never touched across
  users), local-first then best-effort provider revoke, idempotent,
  counts only in response/logs [C: `backend/users.py::erase_user`,
  `http_server.py::erase_user`]. The provider-side meaning of "revoke"
  is the same as F-C2 below. This is **not** `POST /privacy/erase`: the
  installation erase never touches the `users` table (F-C2 intact) [C].
- Backend `POST /privacy/erase` (F-C2, "Borrar mis datos" button in
  Managed mode): deletes **all** connections of the calling
  installation (every provider) + unshared token rows + **all**
  sessions + pending transactions + the installation row itself.
  Local deletion happens first; remote revokes (access + refresh per
  provider) are best-effort afterwards. Idempotent: erasing twice
  returns zero counts. The bearer used for the call is revoked as a
  side effect; `/status` then reports disconnected and reconnecting
  requires a fresh OAuth flow. Response and logs carry counts only.
- Provider-side meaning of "revoke" (verified against current docs,
  2026-09-18):
  - Google: `POST https://oauth2.googleapis.com/revoke` — revoking the
    access token also revokes its paired refresh token.
  - Twitch: `POST https://id.twitch.tv/oauth2/revoke`
    (`client_id` + `token`; `200` = revoked, `400` = already invalid =
    goal achieved). Verify with `GET /validate` (revoked → `401`).
  - Kick: `POST https://id.kick.com/oauth/revoke`
    (`?token=&token_hint_type=`). Verify with
    `POST /oauth/token/introspect` (revoked → `active: false`).
  A surviving shared-account installation gets `SESSION_EXPIRED` on
  next use and must reconnect (documented limitation of shared grants:
  an OAuth grant is per app+user, not per installation).
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

To fully leave: Disconnect each account (revokes + deletes), or use
"Borrar mis datos (Managed)" for a one-shot backend wipe, then
uninstall. Backend rows of an abandoned installation persist until
explicitly revoked/disconnected/erased (limitation, not a silent purge).

The uninstaller shows an informational notice to this effect on start
(it never deletes `%APPDATA%` or backend rows by design).

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

Privacy contact: `horangelmillan@gmail.com`. Do not use public issue
trackers for sensitive reports. How to exercise erasure, fastest first:

1. In OBS (Managed mode): "Borrar mis datos (Managed)" — one-shot
   backend wipe (`POST /privacy/erase`), immediate, with per-provider
   results in the dock.
4. Delete the Managed user account (`POST /users/erase`, dual-auth
   session required): removes the user row and every installation,
   session, connection and unshared token row owned by that user [C].
   v1 has no email, so a lost password cannot be recovered — only
   erased and re-created [C: code stores no email; D: spec §7].
2. Per provider: Disconnect (deletes local + backend rows for that
   connection, best-effort remote revoke).
3. By email (`horangelmillan@gmail.com`): state the installation or
   account concerned; backend rows are deleted manually by the
   operator and provider grants revoked where the provider allows it.
   Reply within one month (RGPD art. 12.3); no art. 17.3 exception
   applies to OAuth tokens/connections (no legal retention duty).

User controls available today: per-provider Disconnect
(deletes local + backend rows, best-effort remote revoke), backend
erase (above), OBS restart persistence semantics (§8–§9), uninstall
scope (§10).

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
| `managedFacebook` + `facebookLiveId` | `/status` + dock (ADR-015) | DPAPI file (plaintext: id+display + live-video ID, sin tokens) | reconnect labels + ID-céntrico F-075 (sobrevive a Disconnect/restart; solo DELETE lo limpia) | until Disconnect (snapshot) / until DELETE (live ID) | snapshot omitido / live ID borrado en DELETE |
| `connectionMode`, `backendBaseUrl` | local/backend | DPAPI file (plaintext) | context/binding | until overwrite | — |
| session token record | `/auth/*` | PG `sessions` | bearer auth | 30 min access TTL; flagged on revoke, physically deleted by the daily F-C4 purge (≤ ~25 h) | purge (expired/revoked) or erase deletes all of the installation |
| OAuth transaction | `/connect/*` | PG `transactions` | CSRF/PKCE/exchange | 600 s TTL, single-use | consume/expiry; erase deletes pending of the installation |
| connection entry | callback | PG `connections` | status/reconnect | until Disconnect/erase | disconnect (per provider) or erase (all) |
| user tokens | exchange/refresh | PG `tokens` (ciphertext `v1:` + metadata en claro) | API calls | until Disconnect/erase (shared rows kept while referenced) | disconnect / erase (guarded) + best-effort provider revoke |
| username + `password_hash` (Managed v1) | `POST /users/signup` | PG `users` (hash `pbkdf2-sha256$600k$salt$hash`; no email) | Managed login identity | until account erase (no auto-expiry) | `POST /users/erase` (user row DELETE) |
| `user_session` token (Managed v1) | `POST /users/login` | PG `sessions` (kind `user`, 30 min TTL; plugin copy DPAPI-persisted `managedUser` desde T-086) | Managed dual auth (`X-User-Session`) | 30 min access TTL; revoked/expired rejected; daily purge (default shared store) | logout (revoke) / purge / account or installation erase |
| `managedUser` (plugin DPAPI, T-086) | login/logout/401/expiry | DPAPI file (encrypted blob: `token`+`expiry`+`username`; CurrentUser; bound to `backendBaseUrl`) | skip login on OBS start when valid | until logout/401/30 min expiry (backend TTL governs even if the file still exists) | logout/401/expiry clears the record |
| `user_id` / `owner_user_id` (Managed v1) | adoption at login/signup | PG `installations`/`sessions`/`connections`/`tokens` (nullable pre-adoption) | scope rows to their user | with the row | adoption sets; erases delete |
| installation record | `/auth/bootstrap` | PG `installations` | backend auth | until erase | erase (row DELETE); revoke flags only |
| provider client secrets | operator Secret Manager | Secret Manager → files | OAuth | managed externally | operator rotation |
| `DATABASE_URL` | operator Secret Manager | Secret Manager env | DB access | managed externally | operator rotation |
| title/description text | user typing | memory only (+ provider on Apply) | metadata update | transient | — |
| request logs | runtime | stdout/Cloud Logging | diagnostics | platform default | operator config |

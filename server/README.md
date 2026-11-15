# Discord PS4 account-link backend

Server-side Discord OAuth2 Authorization Code + PKCE. Each PS4 installation starts its own short-lived linking session.

## Security model

- The QR code contains only a random, short-lived session URL.
- The PS4 receives a separate random polling secret; it is never embedded in the QR URL.
- Users authenticate only on Discord's official authorization domain.
- The Discord client secret and refresh token never enter the PS4 package or browser.
- The callback exchanges the one-time authorization code on the Worker.
- Refresh tokens are encrypted with AES-GCM before storage.
- OAuth state and PKCE S256 are checked. The callback also validates Discord's `iss` response parameter.
- The PS4 polls with its separate bearer secret. OAuth tokens are never returned by the status endpoint.
- Current scopes are `identify guilds`: profile and basic server metadata only. This does not grant access to channel messages or DMs.

## Endpoints

- `POST /api/link/start` -> `session_id`, `poll_token`, `qr_url`, expiry and poll interval.
- `GET /link/:session_id` -> English mobile page opened by scanning the QR.
- `GET /auth/discord?session=...` -> Discord OAuth authorization.
- `GET /auth/callback` -> server-side code exchange and encrypted refresh-token storage.
- `GET /api/link/status/:session_id` -> pending/linked state, profile and server list.
- `POST /api/link/refresh/:session_id` -> refreshes the server-side Discord token using the PS4 polling secret.
- `GET /api/health` -> reports whether the required bindings and secrets are configured (never returns secret values).

## Cloudflare setup

1. In Cloudflare, create a Workers KV namespace named `DISCORD_PS4_LINKS`; copy its namespace ID.
2. Set GitHub Actions repository secrets listed below. The deployment workflow substitutes the namespace ID into `wrangler.toml` only in the runner; it is not committed.
3. Create a Discord application in the Discord Developer Portal. Copy its Application ID and Client Secret. Register the exact callback URL:
   `https://discord-ps4-link.<YOUR-WORKERS-SUBDOMAIN>.workers.dev/auth/callback`
4. Set the same callback URL as `DISCORD_REDIRECT_URI`.
5. Generate a cryptographically random 32-byte key, base64url-encode it without padding, and store it as `TOKEN_ENCRYPTION_KEY`.
6. Run the GitHub Actions workflow **Deploy Discord OAuth Worker** manually.
6. Verify `/api/health` reports `configured: true`. Then set the GitHub Actions secret `DISCORD_WORKER_BASE_URL` to the Worker origin and run the native PKG build again.

### GitHub Actions secrets required

- `CLOUDFLARE_API_TOKEN` — for the first deployment, use a token with Workers product-level Admin so Wrangler can create the Worker. After the Worker exists, a Workers Editor token is sufficient for later deployments. The KV namespace is created separately in the dashboard; deploying an existing KV binding does not require direct KV data access.
- `CLOUDFLARE_ACCOUNT_ID`
- `CLOUDFLARE_KV_NAMESPACE_ID`
- `DISCORD_CLIENT_ID`
- `DISCORD_CLIENT_SECRET`
- `DISCORD_REDIRECT_URI`
- `DISCORD_WORKER_BASE_URL` — the Worker origin, such as `https://discord-ps4-link.<YOUR-WORKERS-SUBDOMAIN>.workers.dev` (used by the native PKG build).


Never put any of these values in source code, issues, chat messages, or the PS4 package. Do not share Discord passwords or personal user tokens. The Cloudflare API token and Discord Client Secret should be entered only into the GitHub Actions Secrets UI. The deployment workflow creates the AES-GCM key once in Cloudflare and preserves it across later deployments; it does not rotate the key on each run.

## Important storage behavior

Workers KV is globally distributed and eventually consistent. It is suitable for short-lived session records and token storage, but a status read from another edge location can briefly see an older value after a write. The client must tolerate a short pending state and keep polling. If strict immediate read-after-write behavior is required, move the live session state to Durable Objects while retaining encrypted long-lived token records separately.

## Local checks

```sh
node --check worker.js
node --test worker.test.mjs
```

Deployment is not complete until the account owner configures the Cloudflare and Discord application secrets and the deployment workflow succeeds.

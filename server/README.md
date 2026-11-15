# Discord PS4 account-link backend

Server-side OAuth2 Authorization Code + PKCE for linking each PS4 installation to its own Discord account.

## Security
- QR URL contains only a random, short-lived link-session ID.
- PS4 receives a separate random polling secret from POST /api/link/start; it is never placed in the QR URL.
- Login and consent happen on Discord's official authorization page.
- Discord client secret and refresh token never enter the PS4 package or browser page.
- Callback exchanges the one-time code server-side; refresh token is encrypted with AES-GCM.
- PS4 polls only with its separate bearer secret. Tokens are never returned by the status endpoint.
- OAuth state and PKCE S256 protect the callback against request forgery and code interception.
- Scopes are identify and guilds. This returns profile and basic guild metadata, not guild messages.

## Endpoints
- POST /api/link/start -> session_id, poll_token, qr_url, expires_at
- GET /link/:session_id -> page opened by scanning the QR
- GET /auth/discord?session=... -> Discord OAuth authorization
- GET /auth/callback -> server-side code exchange
- GET /api/link/status/:session_id -> pending/linked state, profile and guild list

## Cloudflare setup
1. Create a Cloudflare Worker named discord-ps4-link.
2. Create a Workers KV namespace and bind it as LINKS.
3. Set DISCORD_CLIENT_ID as a Worker variable.
4. Add DISCORD_CLIENT_SECRET as a Worker secret.
5. Set DISCORD_REDIRECT_URI to the exact callback URL and register the same URL in Discord Developer Portal.
6. Generate a random 32-byte encryption key and add it as TOKEN_ENCRYPTION_KEY secret (base64url text).
7. Deploy the Worker and test /api/health before enabling the PS4 client.

Never commit Discord client secrets, access tokens, or refresh tokens. Never request a user's Discord password or personal token.

## Checks
Run node --check server/worker.js and npm test.

Deployment is not claimed: it requires the owner's Cloudflare account, KV namespace ID, and Discord application credentials.

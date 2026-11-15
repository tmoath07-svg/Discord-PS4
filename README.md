# Discord PS4

Unofficial native PlayStation 4 homebrew interface prototype built with OpenOrbis SDL2.

## Version 1.10\n\n## Current state

- The native app interface is English-only temporarily while Arabic glyph shaping and spacing are being repaired.
- The native Login page now starts a per-device link session against the Cloudflare Worker, creates a real QR locally from the returned session URL, and polls for completion.
- QR content contains only a short-lived session URL. The separate polling secret stays in the PS4 app's memory and is never placed in the QR.
- Discord sign-in takes place on Discord's official OAuth2 authorization page. The Worker handles the authorization-code exchange and keeps client secrets and refresh tokens server-side.
- The Worker URL is a build-time setting supplied through the GitHub Actions secret `DISCORD_WORKER_BASE_URL`. Until the Cloudflare Worker is deployed and this secret is set, the app will show that the endpoint is not configured.
- The Arabic font asset remains packaged for later retesting; no Arabic UI strings are rendered in this build.
- The Servers page shows real linked-account metadata only; it does not fabricate guilds or demo data.

## Discord OAuth2 scope and limitations

The current web OAuth2 flow requests only `identify guilds`. It can retrieve the user's basic profile and the servers they belong to. It does **not** grant access to channel messages, DMs, voice, or private server content.

Discord's separate Social SDK console account-linking integration and its console middleware are a different path. Access to that console middleware requires the appropriate PlayStation partner process and Discord approval. This repository does not include approved PlayStation SDK binaries.

Official references:
- OAuth2: https://github.com/discord/discord-api-docs/blob/main/developers/topics/oauth2.mdx
- Current user and guild endpoints: https://github.com/discord/discord-api-docs/blob/main/developers/resources/user.mdx
- Console account linking: https://github.com/discord/discord-api-docs/blob/main/developers/discord-social-sdk/development-guides/account-linking-on-consoles.mdx
- Console middleware requirements: https://support-dev.discord.com/hc/en-us/articles/30209074764183-How-to-Obtain-Console-Middleware-for-the-Social-SDK

## Cloudflare deployment status

The backend and a manual GitHub Actions deployment workflow are in the repository. Deployment is **not yet complete**: the account owner must create a Workers KV namespace, create the Discord developer application, and add the required Cloudflare/Discord values as GitHub Actions secrets. Never put those values in source code, issues, or chat.

See [server/README.md](server/README.md) for the exact configuration list and deployment sequence.

## PKG build

GitHub Actions downloads OpenOrbis v0.5.4, downloads the pinned Noto Sans Arabic font and OFL license, compiles the ELF, creates and validates the PKG, and uploads Discord-PS4.pkg as artifact discord-ps4-native-pkg.

CI success proves the package was produced and validated, not that OAuth has been deployed or that the app has been tested on a physical PS4.

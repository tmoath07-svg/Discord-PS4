# Discord PS4

An experimental, controller-first Discord-inspired web client targeting PlayStation 4 browsers/homebrew environments.

## Status
Early frontend prototype. This repository does **not** contain an official Discord client, private API access, a PS4 PKG, or a working Discord authentication flow yet.

## Development
Open `index.html` in a modern browser or serve the repository root with any static HTTP server.

## Security and platform notes
- Use Discord's documented OAuth2 and API capabilities only.
- Never put a Discord client secret in browser code.
- Do not automate normal user accounts (self-bots).
- Voice, DMs, friends, and other restricted capabilities depend on Discord's current API permissions and approved application access.
- A valid PS4 PKG requires a compatible PS4 SDK/toolchain and target-environment testing; GitHub Actions web checks alone cannot produce or certify one.

## Current milestone
- Controller-friendly Discord-style shell
- Keyboard/gamepad navigation
- Responsive 1080p layout
- Static frontend checks in GitHub Actions

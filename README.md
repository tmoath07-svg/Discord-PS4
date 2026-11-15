# Discord PS4

Unofficial native PlayStation 4 homebrew interface prototype built with OpenOrbis SDL2.

## Version 1.02
- Discord-inspired login and personal server-list screens.
- Uses the previously PS4-tested FreeType Arabic presentation-form renderer; the newer HarfBuzz renderer was removed after the PS4 build showed missing-glyph boxes.
- The login page clearly indicates that no real QR code is issued yet.
- The server page does not fabricate guilds or display demo server data.
- D-pad left/right switches between Login and Servers; Enter/A selects the current page.

## Real Discord integration status
Real per-user QR login and real guild/channel data require Discord Social SDK. Discord's console device-authorization flow issues a unique user code and QR URL, and the SDK exposes methods such as GetUserGuilds and GetGuildChannels. PlayStation console middleware is distributed only after the developer has a PlayStation Partners account and completes Discord's console middleware request/approval process. This repository currently uses OpenOrbis homebrew and does not contain those approved PlayStation SDK binaries. Therefore this build is the UI foundation, not a working Discord sign-in client.

Official references:
- https://github.com/discord/discord-api-docs/blob/main/developers/discord-social-sdk/development-guides/account-linking-on-consoles.mdx
- https://support-dev.discord.com/hc/en-us/articles/30209074764183-How-to-Obtain-Console-Middleware-for-the-Social-SDK
- https://discord.com/developers/docs/slayer-sdk/classdiscordpp_1_1Client.html

## PKG build
GitHub Actions downloads OpenOrbis v0.5.4, downloads the pinned Noto Sans Arabic font and OFL license, compiles the ELF, creates and validates the PKG, and uploads Discord-PS4.pkg as artifact discord-ps4-native-pkg.

CI success proves the package was produced and validated, not that it has been tested on a physical PS4.

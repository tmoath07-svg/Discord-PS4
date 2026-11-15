# Discord PS4

Native PS4 homebrew interface prototype inspired by Discord's dark desktop layout.

## Current milestone
- Native C++ / SDL2 rendering target.
- 1080p layout with server rail, channel list, chat view and profile panel.
- Basic D-pad / keyboard channel navigation.
- Offline visual shell only: no real Discord login, messaging, voice, friends or DMs.
- Not an official Discord application and not endorsed by Discord.

## Build
GitHub Actions downloads the pinned OpenOrbis v0.5.4 toolchain, verifies its SHA-256, compiles the native SDL2 entry point and attempts to package the application. Artifact: `discord-ps4-native-pkg`.

## Limitations
A successful CI build confirms package creation only, not installation or runtime compatibility on a physical PS4. This is an unsigned homebrew package intended for compatible homebrew environments. Real account features require an approved integration using Discord's documented OAuth2/API capabilities.

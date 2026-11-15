# Discord PS4

Native PlayStation 4 homebrew interface prototype using the OpenOrbis SDL2 sample.

## Current build
- C++ / SDL2 native application.
- 1920x1080 Discord-inspired dark layout.
- Server rail, channel list, chat panel and basic D-pad navigation.
- Offline interface only. It does not authenticate to Discord or send/receive real messages.
- Unofficial project, not affiliated with Discord.

## PKG build
GitHub Actions downloads OpenOrbis v0.5.4, verifies the published SHA-256 digest, replaces the SDL2 sample entry point, compiles the native ELF and creates a real package using the sample Makefile's create-fself, create-gp4 and PkgTool steps. The workflow uploads `Discord-PS4.pkg` as artifact `discord-ps4-native-pkg`.

## Limitations
CI success proves the PKG was produced, not that it installs or runs on a physical PS4. This is a homebrew package for compatible homebrew environments, not an official Sony-signed application. Discord online features are not implemented.

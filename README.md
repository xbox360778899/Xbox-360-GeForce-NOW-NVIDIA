# GeForce NOW-inspired UI Preview for Xbox 360

An unofficial homebrew dashboard concept inspired by the GeForce NOW interface. It has a dark navigation header, featured-game banner, favourites row and library row. The four visible games use real Steam store header artwork downloaded by the Xbox over HTTPS at startup.

This is a visual prototype only. It does not connect to NVIDIA accounts, import a game library, launch games, or provide cloud streaming. Artwork is fetched from public Steam CDN URLs; it is not supplied by an NVIDIA API.

## Network and artwork

The Xbox 360 must already be connected to an internet-enabled network and have a working IPv4 address. Configure the console and router in the Xbox dashboard before launching the app. The title downloads four JPEG images over HTTPS and verifies server certificates using the included `cacert.pem` file. The console clock must be set correctly for certificate validation. If a request, DNS lookup, TLS check, or JPEG decode fails, the application still opens and shows an artwork-offline status.

Download both `geforce_now_ui.xex` and `cacert.pem` from the GitHub Actions artifact, then place them together in the same launch folder on the Xbox. The certificate file is required for HTTPS verification; do not remove it. The artwork is fetched on each launch in this prototype and is not cached between runs.

## Controls

- D-pad left/right: move the selection between game tiles.
- A: select the highlighted tile and show a prototype-only notice; it does not launch or stream the game.
- B: exit the application.

## Build

The project uses Xenosworks XSDK and builds `geforce_now_ui.xex` for Xbox 360. GitHub Actions builds the XSDK PowerPC toolchain, then builds the app together with the XSDK-compatible Mbed TLS and libcurl ports. The artifact includes the XEX and the CA bundle. `stb_image` decodes the downloaded JPEGs and is public domain.

## References

- NVIDIA GeForce NOW: https://www.nvidia.com/en-us/geforce-now/
- NVIDIA games catalog: https://www.nvidia.com/en-us/geforce-now/games/
- XSDK guide: https://github.com/xenosworks/xsdk/blob/main/docs/GUIDE.md
- curl-xenon/XSDK HTTPS notes: https://github.com/xenosworks/curl-xenon/blob/xenon/docs/README-xsdk.md
- Steam store artwork URLs: https://store.steampowered.com/api/appdetails

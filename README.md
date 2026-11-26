# GeForce NOW UI Preview for Xbox 360

An unofficial Xbox 360 home-screen UI prototype inspired by GeForce NOW. This first iteration is a local, offline interface only: the game tiles are illustrative, and no account sign-in, game launching, network connection, or cloud streaming is implemented.

## Controls

- D-pad left/right: move the selection between game tiles.
- A: select the highlighted tile and show the prototype-only notice.
- B: exit the application.

## Build

The project uses Xenosworks XSDK and builds the `geforce_now_ui.xex` Xbox 360 executable. GitHub Actions builds the PowerPC/Xbox target and uploads the XEX as the `geforce-now-xbox360-ui` artifact. The shader files in `shaders/` are compiled into the application during CMake configuration.

## Prototype status

This is the UI foundation for gradual follow-up work. Streaming and real game-library integration are intentionally out of scope until this screen builds and runs reliably on hardware or an emulator.

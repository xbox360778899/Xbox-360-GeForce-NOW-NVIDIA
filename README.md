# GeForce NOW-inspired UI Preview for Xbox 360

An unofficial homebrew dashboard concept inspired by the GeForce NOW interface. It has a dark navigation header, featured-game banner, favourites row and library row. The four visible games use real Steam store header artwork downloaded by the Xbox over HTTPS at startup.

This is a visual prototype only. It does not connect to NVIDIA accounts, import a game library, launch games, or provide cloud streaming. Artwork is fetched from public Steam CDN URLs; it is not supplied by an NVIDIA API.

I haven't added any clients to it yet; I've been testing it, and I won't add anyone until it becomes stable. Also, I intend to keep it closed-source.

Download both `geforce_now_ui.xex` and `cacert.pem` from the GitHub Actions artifact, then place them together in the same launch folder on the Xbox. The certificate file is required for HTTPS verification; do not remove it. The artwork is fetched on each launch in this prototype and is not cached between runs.

## Controls

- D-pad left/right: move the selection between game tiles.
- A: select the highlighted tile and show a prototype-only notice; it does not launch or stream the game.
- B: exit the application.





First release of **fceuxs5**: the FCEUX NES/Famicom emulator as a native PS5 app.

- FCEUX 2.8 core: NES, Famicom, Famicom Disk System (needs `disksys.rom`), VS. System, UNIF, NES 2.0
- Three interfaces to choose from: **Channel**, **Station** and **Flow** (a 3D cover flow); you pick one at the first start and can change it in Settings
- An fceuxs5 demo intro at start-up (Cross skips it) and looping menu music
- Box art downloaded automatically from libretro-thumbnails
- CRT shaders, plus FCEUX's hq2x, hq3x, Scale2x, Scale3x and Blargg NTSC upscalers
- ROM hacks: IPS, UPS and BPS patches applied when the game starts; your ROM file isn't changed
- Save states, rewind, fast forward, turbo, 4 players, button remapping, .zip/.7z, USB and network shares

**Install:** unzip `fceuxs5-v1.0-PPSA51855.zip` into `/data/homebrew/`, or send `fceuxs5-v1.0.elf` to your ELF loader (port 9021). Put your games in `/data/fceuxs5/roms/`.

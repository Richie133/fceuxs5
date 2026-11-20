# fceuxs5 — the PS5 port layer

This folder turns FCEUX (`../src`) into a native PS5 app. See the [top-level README](../README.md) for what the app does and how to build it.

| Folder | What it holds |
|---|---|
| `fceux/` | The FCEUX platform driver: the `FCEUD_*` hooks, the palette, the pads and messages (GPL-2.0-or-later) |
| `frontend/` | The app: the launcher and its three interfaces (`fe_theme`, `fe_shelf`), the FCEUX glue (`fe_emu`), the intro and music (`fe_media`), ROM-hack patching (`fe_patch`), menus, settings, covers, network shares |
| `coreorbis/` | The PS5 platform: video out, audio out, pads, the sandbox helper, crash logs, paths, the entry point |
| `installer/` | The installer payload, and the helper that downloads covers and updates and leaves the sandbox |
| `proto/native/` | The native-app tooling: links and signs `eboot.bin` and makes `sce_module/libc.prx` (from drakmor/ps5-native-app-boilerplate, GPL-3.0) |
| `host/` | A fake PS5 underneath the app, so it can run and be tested on Linux (`make host`) |
| `tools/` | Import checks, the release zip, database generators, `make_intro.py`, `make_art.py` |

Most of `coreorbis/`, `frontend/`, `installer/` and `netfs/` comes from MisterTemaki's [fceummPS5](https://github.com/MisterTemaki/fceummPS5) (MIT). Each file keeps its notice. The CRT shaders' own origins are in `THIRD_PARTY_SHADERS.md`.

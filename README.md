ECellWolf
=========

A native homebrew port of [ECWolf](https://github.com/ECWolfEngine/ECWolf) for the PS3.

Lineage
-------

ECWolf is an actively maintained, advanced source port of Wolfenstein 3D and
Spear of Destiny, built on id Software's original 1992 source release. This
project adds a full PS3 platform layer on top of it (video, audio, input,
system integration). In short:

```
Wolfenstein 3D (id Software, 1992) -> ECWolf (source port) -> ECellWolf (this project)
```

Features
--------

- **One package, one launcher** (1.1): Wolfenstein 3D (full or
  shareware), Spear of Destiny (and its two mission packs, and the demo)
  and Super 3-D Noah's Ark in a single install. At start a menu lists what
  is installed (skipped when there is only one thing to play); Quit from
  the game's menu comes back to it.
- **Mods:** each folder in `USRDIR/mods/` is an entry on the launcher's
  MODS tab (L1/R1), its `.pk3`, `.zip` and `.wad` files loaded on top of
  the game. See [Installation](#mods).
- **Video:** internal resolution 640x400, scaled by the RSX to whatever
  resolution the TV reports, with a 5% margin on each side for TVs that
  crop the edges. The 8 bit picture goes to the RSX as it is and is drawn
  through the palette there.
- **GPU renderer** (1.1): the 3D view is drawn by the RSX, the same
  picture as ECWolf's software renderer (its textures, palette and light
  tables, square texels), with an optional smooth texture filter.
  Options -> Display Options -> GPU Renderer switches between it and the
  software renderer in game, no restart; Smooth Textures turns on the
  filter. Both are saved in the config.
- **Audio:** 48000 Hz on the PS3's audio port, OPL (AdLib) music and
  digitized sound effects.
- **No SDL** (1.1): the PS3 layer is native (video on the RSX, the
  DualShock 3, the audio port), built with the standard PSL1GHT
  toolchain.
- **Input:** DualShock 3 for movement, look, and every action. Every action
  is rebindable from the in-game Customize Controls menu, including the
  quicksave/quickload shortcuts this port adds.
- **Compatibility:** Wolfenstein 3D (shareware or full/registered), Spear
  of Destiny with its mission packs, Super 3-D Noah's Ark.
- Clean exit to the XMB from the in-game Quit option or the PS button.

Requirements
------------

- A PS3 capable of running homebrew (either HEN or CFW works fine).
- Your own legally-owned copy of Wolfenstein 3D and/or Spear of Destiny. No
  game data is included in this repository or in any release build -- see
  Installation.
- To build from source: the standard ps3dev PSL1GHT toolchain (`ppu-gcc`,
  `ppu-g++`, PSL1GHT SDK, `make_self_npdrm`, `pkg.py`). No SDL2 needed.

Installation
------------

Install `ecellwolf-1.1.pkg` from the XMB (shows up as "ECellWolf"). This
creates `/dev_hdd0/game/ECWOLF001/USRDIR/`. By FTP, place your own game
data there, alongside the `ecwolf.pk3` that already ships in the package.
Each game's files have their own extension, so all of them can share the
folder:

```
ECWOLF001/USRDIR/AUDIOHED.WL6 (.WL1 for the shareware episode)
ECWOLF001/USRDIR/AUDIOT.WL6
ECWOLF001/USRDIR/GAMEMAPS.WL6
ECWOLF001/USRDIR/MAPHEAD.WL6
ECWOLF001/USRDIR/VGADICT.WL6
ECWOLF001/USRDIR/VGAGRAPH.WL6
ECWOLF001/USRDIR/VGAHEAD.WL6
ECWOLF001/USRDIR/VSWAP.WL6

ECWOLF001/USRDIR/AUDIOHED.SOD
...                                  (the same eight files, .SOD)
ECWOLF001/USRDIR/VSWAP.SOD
```

`CONFIG.WL6` / `CONFIG.SOD` aren't needed -- ECWolf keeps its own
configuration file. Spear of Destiny's mission packs are `.SD2` (Return to
Danger) and `.SD3` (Ultimate Challenge), the Spear demo `.SDM`, Super 3-D
Noah's Ark `.N3D`.

If you have the Steam release of Spear of Destiny, the mission's data lives
under `base/m1/` in its install folder -- that's the one to copy. Steam's
`Spear.exe` and `CONFIG.SOD` are not needed by ECWolf.

**Coming from 1.0:** Wolfenstein 3D's install is this one (same folder,
same saves and settings). Spear of Destiny files still in the folder of
1.0's separate package (`/dev_hdd0/game/ECWOLFSOD/USRDIR/`) are found
there too, so that package can be deleted from the XMB once 1.1 is in --
deleting it also deletes that folder, so move the `.SOD` files over first.

### Mods

Each folder in `ECWOLF001/USRDIR/mods/` is one entry on the launcher's
MODS tab. Its `.pk3`, `.zip` and `.wad` files are loaded in alphabetical
order on top of the game (ECWolf's `--file`). An optional `mod.txt` in the
folder:

```
name = My Mod        the name shown
data = wl6           the game it plays on (default: the first installed)
```

A vanilla mod that ships its own data files with its own extension
(Needle of Tears' `VSWAP.NDM`, `GAMEMAPS.NDM`...) goes in a folder the same
way, all its files together: the launcher loads its `VSWAP`, `GAMEMAPS`,
`VGAGRAPH` and `AUDIOT`, ECWolf finds the `MAPHEAD`, `VGAHEAD`, `VGADICT`
and `AUDIOHED` next to them, and their lumps are named as the base game's
are. The mod's `.EXE` isn't used: whatever it changed in the game's code
(new enemies, behaviour) ECWolf doesn't do.

Filenames are case-sensitive. `VSWAP.WL6` and `vswap.wl6` are two different
files as far as the PS3's filesystem is concerned -- if a file isn't showing
up, this is the first thing to check.

Building from source
---------------------

With the standard ps3dev toolchain (`PS3DEV` set, `ppu-gcc` on the
`PATH`), from the root of the tree:

```
make -C ps3                 # -> ps3/ecellwolf-1.1.pkg
make -C ps3 DEBUG_BUILD=1   # -> ps3/ecellwolf-debug-1.1.pkg
```

The `ps3/` directory holds everything specific to this port:

- **`Makefile`** -- builds the engine, its libraries (bzip2, lzma, gdtoa,
  jpeg from `deps/`), the PS3 layer and `ecwolf.pk3` (`wadsrc/static`
  zipped), and packages them with `ppu_rules`.
- **`compat/`** -- a tiny "SDL 2", "SDL_mixer" and "SDL_net": headers
  declaring only what ECWolf uses, first in the include path, so the
  engine builds with almost no changes.
- **`source/`** -- the PS3 layer: `sdl_ps3.c` (clock, mutexes, the
  DualShock 3 as a joystick), `sdl_mixer_ps3.c` (the audio port),
  `ps3_display.c` and `ps3_gpu.c` (the RSX), `ps3_launcher.c`, the log.
  The GPU renderer itself is engine code: `src/r_gpu.cpp`.
- **`include/`** -- what CMake generates for the other platforms
  (`versiondefs.h`, `gitinfo.h`, gdtoa's `arith.h` for big-endian PPC64).
- **`ps3_compat.h`** -- injected into every file with `-include`:
  `stricmp`/`strnicmp`, `PATH_MAX`, and `I_AM_PS3`, the marker ECWolf's
  source checks for the PS3 paths.
- **`icon-wolf3d.png`** -- the XMB icon.

The log is `USRDIR/ecwolf_log.txt` (everything ECWolf prints), the previous
run's kept as `ecwolf_log.old.txt`.

Controls
--------

| Action | Default |
|---|---|
| Move | Left stick |
| Turn / Look | Right stick |
| Attack | R2 |
| Strafe | L1 |
| Run | L2 |
| Use | Cross |
| Next / Prev weapon | D-pad up / down |
| Automap | Select |
| Quicksave / Quickload | Triangle / Circle |
| Open/close menu | Start |

Everything above is rebindable from Options -> Customize Controls.

Known limitations
------------------

- No multiplayer.
- No in-game way to reach ECWolf's built-in developer console, so its
  native keyboard cheats are unreachable in the release builds.

Debug build
-----------

A second package, `ecellwolf-debug-1.1.pkg` (`ECWOLFDBG`, its own folder
and its own copy of the data), built with `make -C ps3 DEBUG_BUILD=1`,
adds three controller shortcuts not present in the release build: L3
toggles god mode, R3 skips to the next level, and R1 gives all weapons and
max ammo. It is intended for testing only.

Credits
-------

- [ECWolf](https://github.com/ECWolfEngine/ECWolf) by Braden "Blzut3"
  Obrzut and contributors -- the engine this project is built on.
- id Software, for releasing the original Wolfenstein 3D source code.

License
-------

GPLv2, inherited from ECWolf. See [LICENSE](LICENSE).

No Wolfenstein 3D or Spear of Destiny data files are included in this
repository. You need your own legally-owned copy of the game(s).

AI disclosure
-------------

This project's code was written collaboratively with Claude (Anthropic),
working through this port with me in real time over many sessions. Every
bit of testing and debugging were made by me on real hardware.

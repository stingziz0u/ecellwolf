[README.md](https://github.com/user-attachments/files/31937310/README.md)
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

- **Video:** internal resolution 640x400, output window scaled to whatever
  resolution the TV reports (tested up to 1080p), 5% overscan compensation,
  triple-buffered presentation via a patched SDL2/RSX driver.
- **Audio:** 48000 Hz, real hardware audio via SDL2_mixer.
- **Input:** DualShock 3 for movement, look, and every action. Every action
  is rebindable from the in-game Customize Controls menu, including the
  quicksave/quickload shortcuts this port adds.
- **Compatibility:** Wolfenstein 3D (shareware or full/registered) and Spear
  of Destiny, shipped as two separate installs so their IWAD detection never
  conflicts with one another.
- Clean exit to the XMB from the in-game Quit option or the PS button.

Requirements
------------

- A PS3 capable of running homebrew (either HEN or CFW works fine).
- Your own legally-owned copy of Wolfenstein 3D and/or Spear of Destiny. No
  game data is included in this repository or in any release build -- see
  Installation.
- To build from source: the ps3dev/ps3aqua PSL1GHT toolchain with a working
  SDL2 (`ppu-gcc`, `ppu-g++`, PSL1GHT SDK, `make_self_npdrm`, `pkg.py`, etc.).

Installation
------------

Two separate packages are provided, since ECWolf's IWAD selector is disabled
and each install expects to find exactly one game's data alongside it.

### Wolfenstein 3D

Install `ecellwolf-wolf3d-1.0.pkg` from the XMB (shows up as "ECellWolf
(Wolfenstein 3D)"). This creates `/dev_hdd0/game/ECWOLF001/USRDIR/`.

By FTP, place your own game data there, alongside the `ecwolf.pk3` that
already ships in the package:

```
ECWOLF001/USRDIR/AUDIOHED.WL6 (or .WL1 for shareware)
ECWOLF001/USRDIR/AUDIOT.WL6
ECWOLF001/USRDIR/GAMEMAPS.WL6
ECWOLF001/USRDIR/MAPHEAD.WL6
ECWOLF001/USRDIR/VGADICT.WL6
ECWOLF001/USRDIR/VGAGRAPH.WL6
ECWOLF001/USRDIR/VGAHEAD.WL6
ECWOLF001/USRDIR/VSWAP.WL6
ECWOLF001/USRDIR/CONFIG.WL6
```

If your install includes `CONFIG.WL6`, it isn't needed -- ECWolf keeps its
own configuration file separately.

### Spear of Destiny

Install `ecellwolf-sod-1.0.pkg` from the XMB (shows up as "ECellWolf (Spear
of Destiny)"). This creates `/dev_hdd0/game/ECWOLFSOD/USRDIR/`.

By FTP, place your own game data there:

```
ECWOLFSOD/USRDIR/AUDIOHED.SOD
ECWOLFSOD/USRDIR/AUDIOT.SOD
ECWOLFSOD/USRDIR/GAMEMAPS.SOD
ECWOLFSOD/USRDIR/MAPHEAD.SOD
ECWOLFSOD/USRDIR/VGADICT.SOD
ECWOLFSOD/USRDIR/VGAGRAPH.SOD
ECWOLFSOD/USRDIR/VGAHEAD.SOD
ECWOLFSOD/USRDIR/VSWAP.SOD
ECWOLF001/USRDIR/CONFIG.SOD
```

If you have the Steam release of Spear of Destiny, the mission's data lives
under `base/m1/` in its install folder -- that's the one to copy. Steam's
`Spear.exe` and `CONFIG.SOD` are not needed by ECWolf.

Filenames are case-sensitive. `VSWAP.WL6` and `vswap.wl6` are two different
files as far as the PS3's filesystem is concerned -- if a file isn't showing
up, this is the first thing to check.

Building from source
---------------------

Building this port requires a full ps3dev/PSL1GHT cross-compilation
environment with a working SDL2 (for example, one built on top of
[SDL2_PSL1GHT](https://github.com/sergiou87/SDL2_PSL1GHT)). Setting up
that environment is outside the scope of this README.

The `ps3/` directory in this repository holds everything specific to this
port's build process, beyond the engine source itself:

- **`ps3-toolchain.cmake`** -- the CMake toolchain file. Points CMake at
  the PPU cross-compiler, forces `ps3_compat.h` into every file compiled
  (see below), and sets the PS3-appropriate optimization flags. Edit the
  `PS3DEV` path at the top to match your own toolchain install location.
- **`ps3_compat.h`** -- a small compatibility header, injected into every
  source file via the compiler's `-include` flag rather than editing each
  file individually. Defines `stricmp`/`strnicmp` (newlib only provides
  `strcasecmp`/`strncasecmp`) and `I_AM_PS3`, a marker ECWolf's source
  checks to exclude desktop-only code paths (e.g. the KDE file picker) on
  this platform.
- **`Makefile.ecwolfpkg`** -- packages an already-built `.elf` into an
  installable `.pkg`/`.gnpdrm.pkg`, using `ppu_rules`' automatic targets
  (strip, sprxlinker, signing, `package_finalize`). It does not compile
  the engine -- run the CMake/`make` step first, in a build directory of
  your own, with `ecwolf.elf` present there, then invoke this Makefile
  from that same directory. `TITLE`, `APPID`, `PKGFILES`, and `ICON0` are
  overridable on the command line, which is how the three variants below
  are told apart.
- **`icon-wolf3d.png`** / **`icon-sod.png`** -- the `ICON0.PNG` used for
  each variant's XMB entry (320x176).

The three variants (Wolfenstein 3D, Spear of Destiny, and the debug
build described below) differ only in the `PS3_GAME_PATH` compile-time
define (which folder under `/dev_hdd0/game/` the binary looks for its
data in -- `ECWOLF001`, `ECWOLFSOD`, or `ECWOLFDBG`), the `-DECWOLF_DEBUG`
CMake flag for the debug build, and the `TITLE`/`APPID`/`ICON0` passed to
`Makefile.ecwolfpkg` when packaging. The rest of the build process is
identical across all three.

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

- The in-game IWAD selector is disabled -- each package expects its own
  game's data in its own folder, as described in Installation.
- No in-game way to reach ECWolf's built-in developer console, so its
  native keyboard cheats are unreachable in the release builds.

Debug build
-----------

A third package, `ecellwolf-debug-1.0.pkg` (`ECWOLFDBG`), is built with
`-DECWOLF_DEBUG=ON` and adds three controller shortcuts not present in the
release builds: L3 toggles god mode, R3 skips to the next level, and L1
gives all weapons and max ammo. It is intended for testing only.

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

# r1q2_race

A Quake II client based on R1Q2, with jump and race features adapted from
Q2PRO Race.

## Features

- Strafe helper, speedometer, and acceleration efficiency display.
- HUD layout editor, input display, and network graphs.
- FPS presets and jump bindings.
- Server browser with favorites, player lists, and ping.
- DM2 demo browser and playback.
- Widescreen support, borderless fullscreen, and Windows Raw Input.
- Console completion and persistent command history.

Music playback, replacement cinematics, OpenAL audio, and online chat translation
are available when included in the build. Translation is disabled by default.

## Installation

The standalone Windows client requires an existing Quake II installation with
original game data. Jump maps and other game assets are supplied separately.

1. Copy `r1q2_race.exe` into your Quake II directory, beside `baseq2/`.
2. Copy the package's `jump/` folder into the same directory, merging it with
   an existing `jump/` folder if present.
3. Launch `r1q2_race.exe`. Jump is selected by default.

The Windows package includes the renderer; no separate R1GL download is needed.
A local Jump game DLL is not required to join a remote server.

## Playing

Open **Multiplayer > browse jump servers**, or enter `connect ADDRESS:PORT`
in the console with the server's address. The browser also supports favorites
through **Multiplayer > browse address book**.

| Setting | Menu |
| --- | --- |
| HUD and movement overlays | Options > jump settings |
| HUD layout | Options > jump settings > Edit HUD Layout |
| FPS and jump bindings | Options > jump settings > Controls & FPS |
| Mouse and key bindings | Options > input setup / key bindings |
| Display and graphics | Options > video setup |
| Recorded demos | Multiplayer > browse demos |

During a game, press **Escape > HUD & Jump Settings** to access the Jump controls.
The console commands `hud_edit`, `pushmenu servers`, and `pushmenu demos` open
the editor and browsers directly.

On Windows, `m_rawinput 1` enables Raw Input mouse movement. The setting is saved
and takes effect immediately; `m_rawinput 0` restores the previous input path.

Jump menus and overlays are active in the `jump` game directory. To select the
original game directory, add `+set game baseq2` to your shortcut's launch options.
Use `+set basedir "C:/Games/Quake2"` if the game data is stored elsewhere.

## Building

<details>
<summary>Build from source</summary>

The standalone Windows target requires Visual Studio with the Desktop development
with C++ workload, a Windows SDK, CMake 3.24+, and Python 3.10+. From the repository
root, using a Visual Studio generator:

```text
cmake -S . -B build/standalone -A Win32
cmake --build build/standalone --config Release --target r1q2 --parallel
```

Output: `build/standalone/Release/r1q2_race.exe`.
The first configuration/build downloads pinned dependencies. The renderer,
third-party libraries, and Microsoft C runtime are linked into the executable.
Copy the contents of `assets/jump/` to your installation's `jump/` directory.

Meson is also supported with an x86 MSVC toolchain. Feature options are listed in
[CMakeLists.txt](CMakeLists.txt) and [meson.options](meson.options); OpenAL and
FFmpeg support require additional static libraries.

Legacy Unix targets use `make -C binaries`. See the
[build recipes](binaries/Makefile). The Unix server browser supports numeric
addresses, favorites, local lists, and LAN discovery; online lists and hostname
lookup require the Windows backend.

</details>

## Credits and license

Based on R1Q2 by Rich (R1CH), with adaptations from Q2PRO and Q2PRO Race.
Original source notices are retained; see [r1q2.txt](r1q2.txt) for R1Q2 attribution.

See [COPYING](COPYING) for GPL terms and [client/jump/LICENSE](client/jump/LICENSE)
for the imported strafe helper's MPL terms. Licenses vary by file; the preserved
[donor note](NOTE) records that distinction.

# r1q2_race

A Quake II client based on [R1Q2](https://github.com/tastyspleen/r1q2-archive),
with race and jump features adapted from Q2PRO Race: HUD editing, strafe and
speed overlays, network diagnostics, and a server browser.

Quake II and Jump assets are supplied separately. The default game is `jump`;
use `+set game baseq2` for the original game.

Movement prediction uses `cl_async 2` by default.
`gl_zfar 0` sizes the far clip distance to the map; positive values override it.

## Build

Windows x86 requires Visual Studio with C++ support, CMake 3.24+, and Python 3.10+.

```text
cmake -S . -B build/standalone -A Win32 -DR1Q2_CHAT_TRANSLATOR=OFF
cmake --build build/standalone --config Release --target r1q2
```

Output: `build/standalone/Release/r1q2_race.exe`.
Dependencies are downloaded on the first build and linked statically.

Meson is also supported from an x86 MSVC developer prompt:

```text
meson setup build/meson-win32 --buildtype=release -Dchat_translator=false
meson compile -C build/meson-win32 r1q2_race
```

On Unix, use `make -C binaries` with Python 3.10+; the client needs zlib, and
the renderer needs SDL2, OpenGL, libpng, and libjpeg. OpenAL and FFmpeg are
optional; see `CMakeLists.txt`, `meson.options`, or the target Makefile for options.

## Version

Builds use `R1Q2 Race <commit count> ups`, with the commit hash in diagnostics.
Local changes add `[modified]`. Full Git history is needed for the count;
shallow checkouts and source archives show `unknown ups`.

## License

Original source notices are retained. See [COPYING](COPYING) for GPL terms
and [client/jump/LICENSE](client/jump/LICENSE) for the imported strafe helper's
MPL terms. Licenses vary by file; the preserved donor [NOTE](NOTE) records
that boundary. Race adaptations use Q2PRO Race snapshot `5571a17dbb7f`.

# Lichii custom (Ocarina of Time performance build)

A performance build based on [Lichii](https://github.com/zortos293/Lichii), for The Legend of Zelda:
Ocarina of Time on Switch 2 (`0400486024DEC000`). It includes work from the
[Frizti](https://github.com/Frizti/Lichii) fork. The upstream README is in
[README.upstream.md](README.upstream.md).

> [!IMPORTANT]
> No game files are included: you need your own dump of the game. A `.nup` dump needs **no keys and
> no firmware**: the emulator supplies the firmware it needs.
> Tested on one PC only (Ryzen 7 9800X3D, RTX 5070 Ti, Windows 11). It tries to work everywhere, but
> AMD and Intel GPUs and slower CPUs have not been tested. Report problems as issues, with your
> `eden_log.txt`.

## Download and play

1. Download `Lichii-custom-Windows-x64.zip` from the [latest release](../../releases/latest) and unpack it.
2. If Windows says a DLL is missing, install the
   [Microsoft Visual C++ Redistributable (x64)](https://aka.ms/vs/17/release/vc_redist.x64.exe).
3. Run `lichii.exe`, then **File > Load File** and pick your `.nup` (or add its folder to the game list). Settings, keys, saves and games from Lichii, Eden or another Frizti build are used
   as they are (`%APPDATA%\eden`).
4. When it asks for Ocarina's frame rate, pick **60 FPS** (30 FPS is for slow PCs).

The tuned settings are built in; nothing needs to be set by hand.

## What this build changes

Measured on the PC above, in a scripted run through Kokiri Forest (hidden benchmark, not the window):
house about 50 -> 66 fps, village 48 -> 54 fps with the camera turning, 42 -> 52 fps standing, on a
25,000-pipeline shader cache.

- **Shaders on demand.** The cached pipelines are no longer all rebuilt after every boot (that took
  about 10 minutes and cost frame rate the whole time). Each one is built by a worker the first time
  the game needs it, and the draw is skipped until then, unless skipping it would lose a one-time pass.
- **No 60 fps cap** in Ocarina's 60 FPS mode with the speed limit on.
- **Occlusion reports answered at once**, without waiting for the GPU.
- **Fast draw**: draws whose state did not change reuse what the previous draw set up.
- From upstream: #446 (suspended channels polled every 50 us), #550, #481, #580, #570; and
  monarchy/Lichii-performance's SSA alias pass (less shader compile work).
- A hitch recorder: every frame over 50 ms writes a `Hitch:` line in the log with what ran during it.

## Turning things off

`%APPDATA%\eden\nx2_switches.txt` overrides the built-in settings, one `NAME=VALUE` per line. If
something looks wrong, try these first:

| Symptom | Line to add |
|---|---|
| Objects flicker or pop in | `EDEN_NX2_OQ_FAKE=0` |
| Objects missing for a moment in new places | `EDEN_NX2_PIPELINE_ON_DEMAND=0` |
| Broken geometry | `EDEN_NX2_FAST_DRAW=0` |
| Smoother image, a bit slower | nothing: the game's anti-aliasing is on by default here |

Faster but changes the image: `EDEN_NX2_OOT_NO_AA=1` skips the game's anti-aliasing network.

## Building

As upstream: see [README.upstream.md](README.upstream.md). The release was built with clang-cl, the
upstream PGO profile and ThinLTO.

## Notes

Shared as is, with no guarantee. Not affiliated with Nintendo.

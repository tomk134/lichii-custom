# Lichii — personal build for one PC (Ocarina of Time)

This is a personal fork of [Lichii](https://github.com/zortos293/Lichii) (itself based on Eglantine, Eden, yuzu and Sudachi), with changes made for exactly one machine and one game: *The Legend of Zelda: Ocarina of Time* on Switch 2.

**It is built for this PC and this PC only. It may not work for anyone else, and there is no support.**

- Windows 11
- Ryzen 7 9800X3D
- RTX 5070 Ti 16 GB
- 32 GB RAM
- 1440p / 360 Hz

Everything here is tuned, measured and tested on that hardware alone. Other GPUs, other CPUs, other games and other settings were never tried. Expect crashes, missing graphics or no gain at all elsewhere. Use the official Lichii builds instead.

## What is different from upstream Lichii

- Open ports of upstream pull requests that cut texture re-uploads when turning the camera and avoid shader-compile stalls.
- Several experimental GPU-thread fast paths turned on through `nx2_switches.txt` (see `dist/` and the `EDEN_NX2_*` variables in the source).
- Extra opt-in diagnostics for the GPU thread split and the guest scheduler.
- Benchmark-driven changes to the waits between the two GPU threads. Some are kept but left off because they measured neutral.

Nothing is cleaned up for general use. The upstream README is in `README.upstream.md`.

## License

GPL-3.0-or-later, as upstream. See `LICENSE.txt`.

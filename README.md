# fbv-ng

A framebuffer image viewer for Linux, rewritten in C++20 from
[fbv](https://github.com/angeloc/fbv). It keeps fbv's options and keys, and adds animated GIFs,
alpha blending and a decoder core built to survive hostile files. The binary is installed as
`fbv`, a drop-in replacement for the original.

## Features

* BMP, PNG, JPEG and GIF, each one optional at build time
* Animated GIFs with frame offsets, transparency, every disposal method and the loop count
* 8, 15, 16, 24 and 32 bpp framebuffers, using the channel layout the driver reports
* Alpha blending against the screen (`-a`), including transparent animation frames
* Clean console handling: VT switching, terminal state restored on exit and on signals

## Security

Images often come from untrusted places, so the decoders are written defensively:

* every decoder parses an in-memory buffer through bounds-checked readers
* image sizes are validated before any allocation: at most 65536 pixels per side, 64 Mpixel
  and 256 MiB per file
* GIF frames stay indexed and are composited on the fly, so an animation costs about what
  the file costs instead of one canvas per frame
* the `setjmp`/`longjmp` error paths of libpng and libjpeg are confined to small functions
  without C++ objects
* builds are hardened by default (PIE, full RELRO, stack protector, `_FORTIFY_SOURCE`,
  `_GLIBCXX_ASSERTIONS`), every flag probed so any GCC or Clang toolchain takes it
* the tests run under AddressSanitizer and UBSan, and a libFuzzer target is included

## Requirements

fbv draws through the Linux framebuffer device, `/dev/fb0` or `$FRAMEBUFFER`, so the kernel must
provide one:

* a DRM/KMS display driver with fbdev emulation, as most kernels have:
  `CONFIG_DRM_FBDEV_EMULATION=y` and `CONFIG_FB_DEVICE=y` (an option since Linux 6.6), or
* a classic fbdev driver, e.g. `efifb`, `vesafb` or `simplefb`.

On kernels built without fbdev emulation only `/dev/dri/card*` exists and fbv cannot show
anything. The user running fbv needs write access to the device, usually through the `video`
group.

## Building

Requirements: CMake 3.16 or later, a C++20 compiler (GCC 10, Clang 12 or later), and the
libraries of the formats you enable.

```
cmake -S . -B build
cmake --build build
ctest --test-dir build
cmake --install build
```

### Options

| Option             | Default | Description                                      |
|--------------------|---------|--------------------------------------------------|
| `FBVNG_WITH_BMP`   | `ON`    | BMP support, built in                            |
| `FBVNG_WITH_PNG`   | `ON`    | PNG support, needs libpng 1.6                    |
| `FBVNG_WITH_JPEG`  | `ON`    | JPEG support, needs libjpeg or libjpeg-turbo     |
| `FBVNG_WITH_GIF`   | `ON`    | GIF support, needs giflib 5.1 or later           |
| `FBVNG_HARDENING`  | `ON`    | Compiler and linker hardening                    |
| `FBVNG_SANITIZE`   | `OFF`   | AddressSanitizer and UBSan                       |
| `FBVNG_FUZZ`       | `OFF`   | libFuzzer target `fbv-ng-fuzz` (Clang)           |
| `BUILD_TESTING`    | `ON`    | Test programs                                    |

Enable only the formats you need to keep the binary and its dependencies small, e.g. a
BMP-only viewer with no image libraries at all:

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=MinSizeRel \
	-DFBVNG_WITH_PNG=OFF -DFBVNG_WITH_JPEG=OFF -DFBVNG_WITH_GIF=OFF
```

### Binary size

Stripped `fbv` built with `-DCMAKE_BUILD_TYPE=MinSizeRel`, GCC 11.4 on x86_64, image
libraries linked as shared libraries:

| Formats        | Size     | Image libraries needed           |
|----------------|----------|----------------------------------|
| BMP only       | 54 KiB   | none                             |
| all four       | 70 KiB   | libpng, libjpeg, giflib          |

Every build also needs libstdc++ and the C library.

### Cross compiling

Use a toolchain file as usual; libraries are looked up in its sysroot:

```
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=/path/to/toolchain.cmake
```

Buildroot (`cmake-package`) and Yocto (`inherit cmake`) need nothing else. The tests run
through `CMAKE_CROSSCOMPILING_EMULATOR` when it is set, e.g. to `qemu-arm`; the framebuffer
tests only run in native builds. The decoders build on any POSIX system, the viewer itself
needs Linux.

## Usage

```
fbv [options] image...
```

Run `fbv -h` or see `man fbv` for the options and keys.

## Control socket

With `--control <path>` fbv listens on a Unix socket and keeps running until told to quit, so
other programs can drive the display, e.g. a boot splash:

```
fbv -y -c -i -g --control /run/fbv.sock boot.gif &
echo "progress 40" | socat - UNIX-CONNECT:/run/fbv.sock
```

Each command is one line, answered by `ok` or `error <reason>`:

| Command            | Effect                                                            |
|--------------------|-------------------------------------------------------------------|
| `ping`             | check that fbv is alive                                           |
| `pause`, `resume`  | stop and restart the animation; `resume` also ends a held frame   |
| `frame <n>`        | show frame n (from 0) and hold it                                 |
| `progress <0-100>` | step to the frame matching the percentage at the animation's pace |
| `load <file>`      | show another image or animation, keeping the current one on error |
| `status`           | `ok frame=<n> frames=<count> paused=<0\|1>`                       |
| `quit [retain]`    | exit; `retain` leaves the console in graphics mode                |

`-g` switches the console to graphics mode while fbv runs, so no kernel text or cursor is drawn
over the image. The socket is created for its owner only.

## Testing

`ctest` runs:

* reference decodes of the images in `tests/data`, every frame and frame delay
* the [gif-test-suite](https://github.com/peterdn/gif-test-suite) by Peter Nelson
* unit tests of the transforms, size limits and malformed headers
* truncated and corrupted copies of every test image
* the viewer on a fake framebuffer at 32, 24 and 16 bpp
* the control socket commands on a fake framebuffer

For the full check build with `-DFBVNG_SANITIZE=ON`. To fuzz with Clang:

```
CXX=clang++ cmake -S . -B fuzz -DFBVNG_FUZZ=ON -DFBVNG_SANITIZE=ON
cmake --build fuzz --target fbv-ng-fuzz
./fuzz/fbv-ng-fuzz tests/data tests/gif-test-suite
```

`tests/gen_data.py` regenerates `tests/data` and its references with Pillow.

## License

GPL-2.0-or-later, see `LICENSE`. `tests/gif-test-suite` is BSD-2-Clause, see its `LICENSE`.

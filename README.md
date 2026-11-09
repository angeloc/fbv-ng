# fbv-ng

fbv-ng is an image viewer for the Linux framebuffer. It is a rewrite, in C++20, of
[fbv](https://github.com/angeloc/fbv).

It keeps the options and the keys of fbv, and adds:

- animated GIFs;
- alpha blending;
- decoders written to survive hostile files.

The program is installed as `fbv`, so it can replace the original without any other change.

## Features

- It reads BMP, PNG, JPEG and GIF files. You can turn each format on or off when you build.
- It plays animated GIFs, with frame offsets, transparency, every disposal method and the
  loop count.
- It works on 8, 15, 16, 24 and 32 bpp framebuffers, and uses the channel layout that the
  driver reports.
- With `-a`, it blends the image with what is already on the screen. This also works for
  transparent animation frames.
- It handles the console cleanly. It supports VT switching, and it restores the terminal
  when it exits or receives a signal.

## Security

Images often come from places you cannot trust. So the decoders are written defensively:

- Every decoder reads the file from a buffer in memory, through readers that check the
  bounds.
- The image size is checked before any memory is allocated. The limits are 65536 pixels per
  side, 64 megapixels and 256 MiB per file.
- GIF frames stay indexed and are composed when they are drawn. So an animation costs about
  what its file costs, not one canvas per frame.
- libpng and libjpeg report errors with `setjmp`/`longjmp`. This code is kept in small
  functions without C++ objects.
- Builds are hardened by default: PIE, full RELRO, stack protector, `_FORTIFY_SOURCE` and
  `_GLIBCXX_ASSERTIONS`. Each flag is tested first, so any GCC or Clang toolchain can build
  fbv-ng.
- The tests run under AddressSanitizer and UBSan, and a libFuzzer target is included.

## Requirements

fbv draws on the Linux framebuffer device. This is `/dev/fb0`, or the device named in
`$FRAMEBUFFER`. So the kernel must provide one, in one of two ways:

- a DRM/KMS display driver with fbdev emulation, as most kernels have. This needs
  `CONFIG_DRM_FBDEV_EMULATION=y` and `CONFIG_FB_DEVICE=y` (an option since Linux 6.6);
- a classic fbdev driver, for example `efifb`, `vesafb` or `simplefb`.

A kernel built without fbdev emulation only has `/dev/dri/card*`, and fbv cannot show
anything there.

The user who runs fbv needs write access to the device. Usually this means being in the
`video` group.

## Building

You need CMake 3.16 or later, a C++20 compiler (GCC 10, Clang 12 or later), and the
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

To keep the program and its dependencies small, enable only the formats you need. For
example, this builds a BMP-only viewer that needs no image library at all:

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=MinSizeRel \
	-DFBVNG_WITH_PNG=OFF -DFBVNG_WITH_JPEG=OFF -DFBVNG_WITH_GIF=OFF
```

### Binary size

These are the sizes of a stripped `fbv`, built with `-DCMAKE_BUILD_TYPE=MinSizeRel` and
GCC 11.4 on x86_64. The image libraries are linked as shared libraries.

| Formats        | Size     | Image libraries needed           |
|----------------|----------|----------------------------------|
| BMP only       | 54 KiB   | none                             |
| all four       | 70 KiB   | libpng, libjpeg, giflib          |

Every build also needs libstdc++ and the C library.

### Cross compiling

Use a toolchain file, as usual with CMake. The libraries are searched in its sysroot:

```
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=/path/to/toolchain.cmake
```

Buildroot (`cmake-package`) and Yocto (`inherit cmake`) need nothing else.

About the tests in a cross build:

- When `CMAKE_CROSSCOMPILING_EMULATOR` is set, for example to `qemu-arm`, the tests run
  through it.
- The framebuffer tests only run in native builds.

The decoders build on any POSIX system. The viewer itself needs Linux.

## Usage

```
fbv [options] image...
```

Run `fbv -h` or read `man fbv` for the options and the keys.

## Control socket

With `--control <path>`, fbv listens on a Unix socket and keeps running until it is told to
quit. Other programs can then drive the display. One example is a boot splash such as
[yabi](https://github.com/angeloc/yabi), which uses the socket to follow the boot:

```
fbv -y -c -i -g --control /run/fbv.sock boot.gif &
echo "progress 40" | socat - UNIX-CONNECT:/run/fbv.sock
```

Each command is one line. fbv answers `ok` or `error <reason>`.

| Command            | Effect                                                            |
|--------------------|-------------------------------------------------------------------|
| `ping`             | check that fbv is alive                                           |
| `pause`, `resume`  | stop and restart the animation; `resume` also ends a held frame   |
| `frame <n>`        | show frame n (from 0) and hold it                                 |
| `progress <0-100>` | step to the frame matching the percentage at the animation's pace |
| `load <file>`      | show another image or animation, keeping the current one on error |
| `status`           | `ok frame=<n> frames=<count> paused=<0\|1>`                       |
| `quit [retain]`    | exit; `retain` leaves the console in graphics mode                |

Two things to know:

- `-g` switches the console to graphics mode while fbv runs. So the kernel draws no text
  and no cursor over the image.
- The socket is created for its owner only.

## Testing

`ctest` runs these tests:

- the decoding of the images in `tests/data`, compared with reference files for every frame
  and frame delay;
- the [gif-test-suite](https://github.com/peterdn/gif-test-suite) by Peter Nelson;
- unit tests of the transforms, the size limits and malformed headers;
- truncated and corrupted copies of every test image;
- the viewer on a fake framebuffer, at 32, 24 and 16 bpp;
- the control socket commands, on a fake framebuffer.

For the full check, build with `-DFBVNG_SANITIZE=ON`. To fuzz with Clang:

```
CXX=clang++ cmake -S . -B fuzz -DFBVNG_FUZZ=ON -DFBVNG_SANITIZE=ON
cmake --build fuzz --target fbv-ng-fuzz
./fuzz/fbv-ng-fuzz tests/data tests/gif-test-suite
```

`tests/gen_data.py` creates `tests/data` and its reference files again, with Pillow.

## License

GPL-2.0-or-later, see `LICENSE`. `tests/gif-test-suite` is BSD-2-Clause, see its own
`LICENSE`.

# ArduLinux

[![Build](https://github.com/l5yth/ardulinux/actions/workflows/build.yml/badge.svg)](https://github.com/l5yth/ardulinux/actions/workflows/build.yml)
[![Coverage](https://codecov.io/gh/l5yth/ardulinux/branch/main/graph/badge.svg)](https://codecov.io/gh/l5yth/ardulinux)
[![PlatformIO Registry](https://badges.registry.platformio.org/packages/l5y/platform/ardulinux.svg)](https://registry.platformio.org/platforms/l5y/ardulinux)

ArduLinux is a continuation of [portduino](https://github.com/geeksville/framework-portduino) and implements the Arduino API as a Linux user-space library, allowing firmware written for embedded targets (nRF52, ESP32, AVR, etc.) to build and run on Linux without modification. Applications get access to real hardware - GPIO via libgpiod, SPI via spidev, I2C via i2c-dev, Serial via POSIX file descriptors - or fully simulated devices for CI and development.

## Differences from portduino framework

ArduLinux is a clean-room continuation, not a fork. The key differences:

- **No vendored dependencies**: [ArduinoCore-API](https://github.com/arduino/ArduinoCore-API) and [WiFi](https://github.com/arduino-libraries/WiFi) are upstream git submodules, not copied or patched source trees.
- **Self-contained PlatformIO platform**: ships its own `platform.json` and SCons builder; no separate platform-native dependency or private package mirror required.
- **Smaller surface area**: dead code, unused variants, and IDE project files removed.

## Using as a platform (PlatformIO)

Add to your `platformio.ini`:

```ini
[env:ardulinux]
platform = git+https://github.com/l5yth/ardulinux.git
framework = arduino
board = ardulinux
```

Hardware support is gated on **libgpiod**: if `pkg-config` finds it, real GPIO/I2C are compiled in; this also links **libi2c**, so install the two together. If libgpiod is absent, the build uses fully simulated GPIO/I2C and needs no hardware libraries.

## Building standalone (CMake)

Requires GCC or Clang (C++14), CMake 3.17+, and pkg-config. Hardware GPIO/I2C are enabled when libgpiod is detected; libgpiod also requires libi2c, so install both together (or neither, for a simulated build).

ArduinoCore-API and WiFi are git submodules. Clone with them, or initialise them after cloning:
```sh
git clone --recurse-submodules https://github.com/l5yth/ardulinux.git
# already cloned?  →  git submodule update --init --recursive
```

Install the build dependencies. On Debian/Ubuntu:
```sh
sudo apt-get install build-essential cmake libgpiod-dev libi2c-dev pkg-config
```
On Arch:
```sh
sudo pacman -S base-devel cmake libgpiod i2c-tools pkg-config
```

Configure and build:
```sh
cmake -B build
cmake --build build
./build/ArduLinux --help
```

To force a simulated build on a machine that *has* libgpiod installed, hide it from pkg-config with an empty `PKG_CONFIG_LIBDIR` (an empty `PKG_CONFIG_PATH` is not enough):
```sh
PKG_CONFIG_LIBDIR= cmake -B build-sim
cmake --build build-sim
```

### Running the tests

```sh
ctest --test-dir build --output-on-failure
```

The unit tests use Catch2 (fetched automatically at configure time). The I2C hardware tests are built only when libgpiod and libi2c are present.

## Writing an application

Implement the standard Arduino `setup()` and `loop()` functions. Optionally define `ardulinuxSetup()` to bind real hardware before `setup()` runs:

```cpp
#include "Arduino.h"
#include "linux/gpio/LinuxGPIOPin.h"

void ardulinuxSetup() {
    // Bind pin 7 to GPIO chip "gpiochip0", line "PIN_7"
    gpioBind(new LinuxGPIOPin(7, "gpiochip0", "PIN_7"));
}

void setup() {
    pinMode(7, OUTPUT);
}

void loop() {
    digitalWrite(7, !digitalRead(7));
    delay(500);
}
```

Without `ardulinuxSetup()`, all pins default to simulated. All `Print`/`Stream` subclasses (`Serial`, `File`, …) also provide a `printf()` method on top of the standard `print()`/`println()`.

### Command-line options

The built application accepts:

| Option | Description |
| --- | --- |
| `-d`, `--fsdir DIR` | Use `DIR` as the virtual filesystem root. |
| `-e`, `--erase` | Wipe the virtual filesystem before startup. |
| `-V`, `--version` | Print the program name and version. |
| `-?`, `--help` / `--usage` | Show the help / usage message. |

The VFS root defaults to `$XDG_DATA_HOME/<app>/default` (i.e. `~/.local/share/ardulinux/default/`); `--fsdir` overrides it.

### Customizing program identity

The platform reads four optional weak symbols. Define any of them as plain (non-weak) definitions in an application source file to override the defaults. No header required:

```cpp
const char *ardulinuxAppName        = "meshcored";                          // startup msg, VFS dir, libgpiod label (default "ardulinux")
const char *ardulinuxAppVersion     = "1.14.1-linux";                       // shown by --version (default: FIRMWARE_VERSION or "unknown")
const char *ardulinuxAppDescription = "Radio mesh daemon";                  // shown in --help
const char *ardulinuxAppBugAddress  = "https://github.com/myorg/myapp/issues"; // "Report bugs to" line in --help
```

## License

LGPL 2.1 - see [LICENSE](LICENSE).

* Copyright (c) 2011-19 Arduino LLC.
* Copyright (c) 2020-23 Geeksville Industries, LLC
* Copyright (c) 2024-26 jp-bennett
* Copyright (c) 2026-27 l5yth

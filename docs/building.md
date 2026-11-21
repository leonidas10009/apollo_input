# Building ApolloInput Nova

## Requirements

- CMake 3.20 or newer.
- A C++20 compiler.
- Ninja (recommended).
- Windows: MSYS2 UCRT64 and the ViGEmBus driver for live virtual gamepads.
- Linux: write access to `/dev/uinput`.

## Clone

ViGEmClient is a submodule, so clone recursively:

```bash
git clone --recursive https://github.com/leonidas10009/apollo_input.git
cd apollo_input
```

If you already cloned without `--recursive`:

```bash
git submodule update --init --recursive
```

## Windows (MSYS2 UCRT64)

Open an MSYS2 UCRT64 shell from the repository root:

```bash
cmake -S . -B cmake-build-apollo-input -G Ninja
cmake --build cmake-build-apollo-input --parallel
ctest --test-dir cmake-build-apollo-input --output-on-failure
```

The CMake script looks for ViGEmClient in:

1. `third-party/ViGEmClient`,
2. `../../third-party/ViGEmClient` for a nested Apollo checkout,
3. `APOLLO_INPUT_VIGEMCLIENT_DIR`.

To point at another checkout:

```bash
cmake -S . -B cmake-build-apollo-input -G Ninja \
  -DAPOLLO_INPUT_VIGEMCLIENT_DIR=/path/to/ViGEmClient
```

### ViGEmBus driver

ViGEmBus is a separate signed kernel driver and is not bundled. Install the
official 1.22.0 package:

<https://github.com/nefarius/ViGEmBus/releases>

Without the driver, the library still builds and the null backend works, but
the ViGEmBus backend reports `VIGEM_ERROR_BUS_NOT_FOUND`.

## Linux

```bash
cmake -S . -B cmake-build-apollo-input -G Ninja
cmake --build cmake-build-apollo-input --parallel
ctest --test-dir cmake-build-apollo-input --output-on-failure
```

uinput requirements:

```bash
sudo modprobe uinput
sudo usermod -aG input "$USER"
```

Log out and back in after changing groups. A persistent udev rule is preferred
for production use.

## CMake options

| Option | Default | Description |
|---|---|---|
| `APOLLO_INPUT_BUILD_TESTS` | `ON` | Build core and integration tests |
| `APOLLO_INPUT_BUILD_EXAMPLES` | `ON` | Build the CLI demo |
| `APOLLO_INPUT_BUILD_TOOLS` | `ON` | Build the Windows controller tester |
| `APOLLO_INPUT_WITH_VIGEM` | `ON` | Enable the ViGEmBus backend on Windows |
| `APOLLO_INPUT_VIGEMCLIENT_DIR` | empty | Path to a ViGEmClient checkout |

Example minimal build:

```bash
cmake -S . -B build -G Ninja \
  -DAPOLLO_INPUT_BUILD_TESTS=OFF \
  -DAPOLLO_INPUT_BUILD_EXAMPLES=OFF \
  -DAPOLLO_INPUT_BUILD_TOOLS=OFF
cmake --build build --parallel
```

## Build outputs

```text
cmake-build-apollo-input/libapollo-input.a
cmake-build-apollo-input/examples/apollo-input-demo.exe
cmake-build-apollo-input/tests/apollo-input-core-tests.exe
cmake-build-apollo-input/tests/apollo-input-xinput-loopback.exe
cmake-build-apollo-input/tools/apollo-input-controller-tester.exe
```

On Linux the library is `libapollo-input.a` and there is no controller tester.

## Install and consume

```bash
cmake --install cmake-build-apollo-input --prefix /path/to/prefix
```

Consumer project:

```cmake
find_package(apollo-input CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE apollo::input)
```

Or vendor it:

```cmake
add_subdirectory(libs/apollo-input)
target_link_libraries(my_app PRIVATE apollo::input)
```

## Visual tester

Build the C++ tester first, then:

```bash
cd visual-tester
python server.py
```

Or on Windows double-click `visual-tester/ABRIR_TESTER_VISUAL.bat`.

The server looks for the tester in:

1. `../bin/apollo-input-controller-tester.exe`,
2. `../cmake-build-apollo-input/tools/apollo-input-controller-tester.exe`,
3. `../build/tools/apollo-input-controller-tester.exe`,
4. `APOLLO_TESTER_EXE`.

## Troubleshooting

### ViGEmClient not found

Initialize the submodule:

```bash
git submodule update --init --recursive
```

Or pass `-DAPOLLO_INPUT_VIGEMCLIENT_DIR=...`.

### ViGEmBus error 0xE0000001

The driver is not installed or not running. Install ViGEmBus 1.22.0 and reboot.

### `/dev/uinput` is not writable

Check permissions:

```bash
ls -l /dev/uinput
```

Add the user to the `input` group or use a udev rule, then start a new login
session.

### The browser does not show a controller

Press a button on the controller first. Browsers can hide gamepads until they
receive input. Also make sure the page is served from localhost, not opened as
a `file://` URL.

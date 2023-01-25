# Build system instructions

Thread Composability Manager (TCM) uses CMake as build system.

Typical CMake workflow can be used: Configure -> Build -> Test -> Pack (Install)

## Configure

The following project controls are available during the configure stage:
```
TCM_TEST:BOOL - Enable testing (ON by default)
TCM_STRICT:BOOL - Treat compiler warnings as errors (ON by default)
```

Command

```bash
cmake <options> <repo_root>
```

Some useful options:
- `-G <generator>` - specify particular project generator. See `cmake --help` for details.
- `-DCMAKE_BUILD_TYPE=<type>` - specify build type, e.g. `RelWithDebInfo`, `Release`, `Debug`. It is not applicable for multiconfig generators, e.g. for Visual Studio* generator.

## Build

Command

```bash
cmake --build .
```

or for GNU Makefiles generator
```bash
make
```

Tests are also built if `TCM_TEST` was not disabled during configuration.

## Test

Make sure `TCM_TEST` option is not disabled during configuration phase and run after build:

```bash
ctest --output-on-failure
```

## Pack (Install)

The project contains installation and packaging instructions.

In order to create portable development package (zip-archive) run:
```bash
cpack
```

Installation can be done using target `install`:
```bash
make install
```

---
**NOTE**

Installation may change your system environment and can't be easily rolled back.
Use `CMAKE_INSTALL_PREFIX` during configuration phase in order to customize installation folder.

---


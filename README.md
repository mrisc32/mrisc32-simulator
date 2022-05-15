# MRISC32 simulator

This repository contains a simple CPU simulator for the [MRISC32 ISA](https://github.com/mrisc32/mrisc32), written in C++.

![DOOM](https://mrisc32.bitsnbites.eu/media/mrisc32-doom-demo.gif)

*Above: [MRISC32 port of DOOM](https://github.com/mbitsnbites/mc1-doom) running in the simulator.*

## Features

* Implements the complete MRISC32 ISA.
* Is portable (works on Linux, macOS and Windows).
* Implements host OS hooks for newlib, enabling file and console I/O etc.
* Can display a section of the simulator RAM as (animated) graphics.
* Simulates parts of [MC1](https://github.com/mrisc32/mc1) memory mapped I/O (e.g. keyboard and mouse input).

## Installation

### Pre-built binaries

Pre-built binaries for Linux, macOS and Windows are available [here](https://github.com/mrisc32/mrisc32-simulator/releases/latest).

Unpack the archive, and add `mrisc32-simulator/bin` to your PATH environment variable.

### Building from source

Use CMake and a C++ compiler to build the simulator:

```bash
mkdir build
cd build
cmake -DCMAKE_BUILD_TYPE=Release ../sim
cmake --build .
```

## Compiling programs

Use the [MRISC32 GNU toolchain](https://github.com/mrisc32/mrisc32-gnu-toolchain) to compile and link programs for MRISC32.

To make the executable suitable for running in the simulator:

* Pass the `-msim` flag when linking the executable.

For example:

```bash
mrisc32-elf-g++ -O2 -o program.elf program.cpp -msim
```

## Running programs

The ELF32 binary file can be executed by the simulator, like so:

```bash
mr32sim program.elf
```

For additional options and more information, run `mr32sim --help`.

## Debug trace inspector

Debug traces from the simulator (or the [MRISC32-A1](https://github.com/mrisc32/mrisc32-a1) VHDL test bench) can be inspected using `mrisc32-trace-tool.py`. It can be useful for finding differences between different simulation runs.

## Function profiling

It is possible to extract dynamic function profiling information from the simulator.

To do profiling you need to generate a symbol/address map file, e.g. using `mrisc32-elf-readelf` as follows:

```bash
mrisc32-elf-readelf -sW program.elf | grep FUNC | awk '{print $2,$8}' > program-symbols
```

Then run the simulator with the `-P` and `-v` flags as follows, which will print the profiling information when the simulator terminates:

```bash
mr32sim -P program-symbols -v program.elf
```

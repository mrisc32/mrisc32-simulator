# MRISC32 simulator

This repository contains a simple CPU simulator for the [MRISC32 ISA](https://github.com/mrisc32/mrisc32), written in C++.

## Installation

### Pre-built binaries

Pre-built binaries for Linux, macOS and Windows are available [here](https://github.com/mrisc32/mrisc32-simulator/releases/latest).

Unpack the archive, and add `mrisc32-simulator/bin` to your PATH environment variable.

### Building from source

Use CMake and a C++ compiler to build the simulator:

```bash
mkdir build
cd build
cmake -DCMAKE_BUILD_TYPE=Relase ../sim
cmake --build .
```

## Compiling programs

Use the [MRISC32 GNU toolchain](https://github.com/mrisc32/mrisc32-gnu-toolchain) to compile and link programs for MRISC32.

To make the executable suitable for running in the simulator:

* Pass the `-msim` flag when linking the executable.
* Use `mrisc32-elf-objcopy` to convert the ELF executable to a raw binary.

For example:

```bash
mrisc32-elf-g++ -O2 -o program.elf program.cpp -msim
mrisc32-elf-objcopy -O binary program.elf program.bin
```

## Running programs

The raw binary file (`*.bin`) can be executed by the simulator, like so:

```bash
mr32sim program.bin
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
mr32sim -P program-symbols -v program.bin
```

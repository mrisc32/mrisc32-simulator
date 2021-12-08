# MRISC32 simulator

This repository contains a simple CPU simulator for the [MRISC32 ISA](https://github.com/mrisc32/mrisc32), written in C++.

## Building the simulator

Use CMake and a C++ compiler to build the simulator:

```bash
mkdir build
cd build
cmake ../sim
cmake --build .
```

## Compiling programs

Use the [MRISC32 GNU toolchain](https://github.com/mrisc32/mrisc32-gnu-toolchain) to compile and link C, C++ and Assembly source code programs for MRISC32.

To make the executable suitable for running in the simulator, pass the `-msim` flag to `mrisc32-elf-gcc`/`mrisc32-elf-g++` when linking the executable, and use `mrisc32-elf-objdump` to convert the ELF executable to a raw binary.

For example:

```bash
mrisc32-elf-g++ -O2 -o my-program.elf my-program.cpp -msim
mrisc32-elf-objcopy -O binary my-program.elf my-program.bin
```

## Running programs

The final `.bin` file can be loaded into the simulator, like so:

```bash
path/to/mr32sim my-program.bin
```

For more information, run `mr32sim --help`.

## Debug trace inspector

Debug traces from the simulator (or the [MRISC32-A1](https://github.com/mrisc32/mrisc32-a1) VHDL test bench) can be inspected using `mrisc32-trace-tool.py`. It can be useful for finding differences between different simulation runs.

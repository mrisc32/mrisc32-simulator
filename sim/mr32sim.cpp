//--------------------------------------------------------------------------------------------------
// Copyright (c) 2018 Marcus Geelnard
//
// This software is provided 'as-is', without any express or implied warranty. In no event will the
// authors be held liable for any damages arising from the use of this software.
//
// Permission is granted to anyone to use this software for any purpose, including commercial
// applications, and to alter it and redistribute it freely, subject to the following restrictions:
//
//  1. The origin of this software must not be misrepresented; you must not claim that you wrote
//     the original software. If you use this software in a product, an acknowledgment in the
//     product documentation would be appreciated but is not required.
//
//  2. Altered source versions must be plainly marked as such, and must not be misrepresented as
//     being the original software.
//
//  3. This notice may not be removed or altered from any source distribution.
//--------------------------------------------------------------------------------------------------

#include "config.hpp"
#include "cpu_simple.hpp"
#include "elf32.hpp"
#include "gpu.hpp"
#include "perf_symbols.hpp"
#include "ram.hpp"

#include <glad/glad.h>
// Note: Keep this comment to convince clang-format to include glad.h before glfw3.h.
#include <GLFW/glfw3.h>

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <thread>

namespace {
// Address of the start of the simulator program arguments.
//
// Offset | Size | Type   | Meaning
// -------+------+--------+----------
// 0      | 4    | int    | argc
// 4      | 4+   | char** | argv
//
const uint32_t SIM_ARGS_START = 0xfff00000U;
const uint32_t SIM_ARGS_END = 0xffff0000U;

// MC1 keyboard scancodes.
// clang-format off
#define KB_A                0x01c
#define KB_B                0x032
#define KB_C                0x021
#define KB_D                0x023
#define KB_E                0x024
#define KB_F                0x02b
#define KB_G                0x034
#define KB_H                0x033
#define KB_I                0x043
#define KB_J                0x03b
#define KB_K                0x042
#define KB_L                0x04b
#define KB_M                0x03a
#define KB_N                0x031
#define KB_O                0x044
#define KB_P                0x04d
#define KB_Q                0x015
#define KB_R                0x02d
#define KB_S                0x01b
#define KB_T                0x02c
#define KB_U                0x03c
#define KB_V                0x02a
#define KB_W                0x01d
#define KB_X                0x022
#define KB_Y                0x035
#define KB_Z                0x01a
#define KB_0                0x045
#define KB_1                0x016
#define KB_2                0x01e
#define KB_3                0x026
#define KB_4                0x025
#define KB_5                0x02e
#define KB_6                0x036
#define KB_7                0x03d
#define KB_8                0x03e
#define KB_9                0x046

#define KB_SPACE            0x029
#define KB_BACKSPACE        0x066
#define KB_TAB              0x00d
#define KB_LSHIFT           0x012
#define KB_LCTRL            0x014
#define KB_LALT             0x011
#define KB_LMETA            0x11f
#define KB_RSHIFT           0x059
#define KB_RCTRL            0x114
#define KB_RALT             0x111
#define KB_RMETA            0x127
#define KB_ENTER            0x05a
#define KB_ESC              0x076
#define KB_F1               0x005
#define KB_F2               0x006
#define KB_F3               0x004
#define KB_F4               0x00c
#define KB_F5               0x003
#define KB_F6               0x00b
#define KB_F7               0x083
#define KB_F8               0x00a
#define KB_F9               0x001
#define KB_F10              0x009
#define KB_F11              0x078
#define KB_F12              0x007

#define KB_INSERT           0x170
#define KB_HOME             0x16c
#define KB_DEL              0x171
#define KB_END              0x169
#define KB_PGUP             0x17d
#define KB_PGDN             0x17a
#define KB_UP               0x175
#define KB_LEFT             0x16b
#define KB_DOWN             0x172
#define KB_RIGHT            0x174

#define KB_KP_0             0x070
#define KB_KP_1             0x069
#define KB_KP_2             0x072
#define KB_KP_3             0x07a
#define KB_KP_4             0x06b
#define KB_KP_5             0x073
#define KB_KP_6             0x074
#define KB_KP_7             0x06c
#define KB_KP_8             0x075
#define KB_KP_9             0x07d
#define KB_KP_PERIOD        0x071
#define KB_KP_PLUS          0x079
#define KB_KP_MINUS         0x07b
#define KB_KP_MUL           0x07c
#define KB_KP_DIV           0x06d
#define KB_KP_ENTER         0x06e

#define KB_ACPI_POWER       0x137
#define KB_ACPI_SLEEP       0x13f
#define KB_ACPI_WAKE        0x15e

#define KB_MM_NEXT_TRACK    0x14d
#define KB_MM_PREV_TRACK    0x115
#define KB_MM_STOP          0x13b
#define KB_MM_PLAY_PAUSE    0x134
#define KB_MM_MUTE          0x123
#define KB_MM_VOL_UP        0x132
#define KB_MM_VOL_DOWN      0x121
#define KB_MM_MEDIA_SEL     0x150
#define KB_MM_EMAIL         0x148
#define KB_MM_CALCULATOR    0x12b
#define KB_MM_MY_COMPUTER   0x140

#define KB_WWW_SEARCH       0x110
#define KB_WWW_HOME         0x13a
#define KB_WWW_BACK         0x138
#define KB_WWW_FOWRARD      0x130
#define KB_WWW_STOP         0x128
#define KB_WWW_REFRESH      0x120
#define KB_WWW_FAVORITES    0x118
// clang-format on

ram_t* s_ram;

uint32_t s_key_event_count;

uint32_t translate_key(int glfw_key) {
  // TODO(m): Add all the keys...
  switch (glfw_key) {
    // clang-format off
    case GLFW_KEY_A:             return KB_A;
    case GLFW_KEY_B:             return KB_B;
    case GLFW_KEY_C:             return KB_C;
    case GLFW_KEY_D:             return KB_D;
    case GLFW_KEY_E:             return KB_E;
    case GLFW_KEY_F:             return KB_F;
    case GLFW_KEY_G:             return KB_G;
    case GLFW_KEY_H:             return KB_H;
    case GLFW_KEY_I:             return KB_I;
    case GLFW_KEY_J:             return KB_J;
    case GLFW_KEY_K:             return KB_K;
    case GLFW_KEY_L:             return KB_L;
    case GLFW_KEY_M:             return KB_M;
    case GLFW_KEY_N:             return KB_N;
    case GLFW_KEY_O:             return KB_O;
    case GLFW_KEY_P:             return KB_P;
    case GLFW_KEY_Q:             return KB_Q;
    case GLFW_KEY_R:             return KB_R;
    case GLFW_KEY_S:             return KB_S;
    case GLFW_KEY_T:             return KB_T;
    case GLFW_KEY_U:             return KB_U;
    case GLFW_KEY_V:             return KB_V;
    case GLFW_KEY_W:             return KB_W;
    case GLFW_KEY_X:             return KB_X;
    case GLFW_KEY_Y:             return KB_Y;
    case GLFW_KEY_Z:             return KB_Z;
    case GLFW_KEY_0:             return KB_0;
    case GLFW_KEY_1:             return KB_1;
    case GLFW_KEY_2:             return KB_2;
    case GLFW_KEY_3:             return KB_3;
    case GLFW_KEY_4:             return KB_4;
    case GLFW_KEY_5:             return KB_5;
    case GLFW_KEY_6:             return KB_6;
    case GLFW_KEY_7:             return KB_7;
    case GLFW_KEY_8:             return KB_8;
    case GLFW_KEY_9:             return KB_9;
    case GLFW_KEY_SPACE:         return KB_SPACE;
    case GLFW_KEY_BACKSPACE:     return KB_BACKSPACE;
    case GLFW_KEY_TAB:           return KB_TAB;
    case GLFW_KEY_LEFT_SHIFT:    return KB_LSHIFT;
    case GLFW_KEY_LEFT_CONTROL:  return KB_LCTRL;
    case GLFW_KEY_LEFT_ALT:      return KB_LALT;
    case GLFW_KEY_LEFT_SUPER:    return KB_LMETA;
    case GLFW_KEY_RIGHT_SHIFT:   return KB_RSHIFT;
    case GLFW_KEY_RIGHT_CONTROL: return KB_RCTRL;
    case GLFW_KEY_RIGHT_ALT:     return KB_RALT;
    case GLFW_KEY_RIGHT_SUPER:   return KB_RMETA;
    case GLFW_KEY_ENTER:         return KB_ENTER;
    case GLFW_KEY_ESCAPE:        return KB_ESC;
    case GLFW_KEY_F1:            return KB_F1;
    case GLFW_KEY_F2:            return KB_F2;
    case GLFW_KEY_F3:            return KB_F3;
    case GLFW_KEY_F4:            return KB_F4;
    case GLFW_KEY_F5:            return KB_F5;
    case GLFW_KEY_F6:            return KB_F6;
    case GLFW_KEY_F7:            return KB_F7;
    case GLFW_KEY_F8:            return KB_F8;
    case GLFW_KEY_F9:            return KB_F9;
    case GLFW_KEY_F10:           return KB_F10;
    case GLFW_KEY_F11:           return KB_F11;
    case GLFW_KEY_F12:           return KB_F12;
    case GLFW_KEY_INSERT:        return KB_INSERT;
    case GLFW_KEY_HOME:          return KB_HOME;
    case GLFW_KEY_DELETE:        return KB_DEL;
    case GLFW_KEY_END:           return KB_END;
    case GLFW_KEY_PAGE_UP:       return KB_PGUP;
    case GLFW_KEY_PAGE_DOWN:     return KB_PGDN;
    case GLFW_KEY_UP:            return KB_UP;
    case GLFW_KEY_LEFT:          return KB_LEFT;
    case GLFW_KEY_DOWN:          return KB_DOWN;
    case GLFW_KEY_RIGHT:         return KB_RIGHT;
    case GLFW_KEY_KP_0:          return KB_KP_0;
    case GLFW_KEY_KP_1:          return KB_KP_1;
    case GLFW_KEY_KP_2:          return KB_KP_2;
    case GLFW_KEY_KP_3:          return KB_KP_3;
    case GLFW_KEY_KP_4:          return KB_KP_4;
    case GLFW_KEY_KP_5:          return KB_KP_5;
    case GLFW_KEY_KP_6:          return KB_KP_6;
    case GLFW_KEY_KP_7:          return KB_KP_7;
    case GLFW_KEY_KP_8:          return KB_KP_8;
    case GLFW_KEY_KP_9:          return KB_KP_9;
    case GLFW_KEY_KP_DECIMAL:    return KB_KP_PERIOD;
    case GLFW_KEY_KP_ADD:        return KB_KP_PLUS;
    case GLFW_KEY_KP_SUBTRACT:   return KB_KP_MINUS;
    case GLFW_KEY_KP_MULTIPLY:   return KB_KP_MUL;
    case GLFW_KEY_KP_DIVIDE:     return KB_KP_DIV;
    case GLFW_KEY_KP_ENTER:      return KB_KP_ENTER;
      // clang-format on

    default:
      return 0;
  }
}

void keyhandler(GLFWwindow* window, int key, int scancode, int action, int mods) {
  // Unused.
  (void)window;
  (void)scancode;
  (void)mods;

  // Emulate the MC1 keyboard event MMIO interface:
  //  Bits 0-8: Keycode.
  //  Bit  31:  1 = press, 0 = release.
  auto keycode = translate_key(key);
  if (action == GLFW_PRESS || action == GLFW_REPEAT)
    keycode |= 0x80000000u;

  // Store the key event in the circular key event buffer and increment the KEYPTR register.
  ++s_key_event_count;
  s_ram->store32(0xc0000080 + 4 * (s_key_event_count % 16), keycode);
  s_ram->store32(0xc0000030, s_key_event_count);
}

void mousehandler(GLFWwindow* window, double x, double y) {
  // Unused.
  (void)window;

  // Emulate the MC1 mouse position MMIO interface:
  //  Bits 0-15:  x coordinate
  //  Bits 16-31: y coordinate
  auto mousepos = (static_cast<uint32_t>(x) & 0xffffu) | (static_cast<uint32_t>(y) << 16);
  s_ram->store32(0xc0000034, mousepos);
}

void mousebtnhandler(GLFWwindow* window, int button, int action, int mods) {
  // Unused.
  (void)window;
  (void)mods;

  // Emulate the MC1 mouse button MMIO interface:
  //  Bit 0: Left button
  //  Bit 1: Middle button
  //  Bit 2: Right button

  uint32_t state = s_ram->load32(0xc0000038);
  if (button == GLFW_MOUSE_BUTTON_LEFT) {
    if (action == GLFW_PRESS)
      state = state | 1;
    else
      state = state & ~1;
  } else if (button == GLFW_MOUSE_BUTTON_MIDDLE) {
    if (action == GLFW_PRESS)
      state = state | 2;
    else
      state = state & ~2;
  } else if (button == GLFW_MOUSE_BUTTON_RIGHT) {
    if (action == GLFW_PRESS)
      state = state | 4;
    else
      state = state & ~4;
  }
  s_ram->store32(0xc0000038, state);
}

int adaptive_window_scale(GLFWwindow* window, int width, int height) {
  auto* monitor = window ? glfwGetWindowMonitor(window) : glfwGetPrimaryMonitor();
  if (monitor) {
    const auto* vmode = glfwGetVideoMode(monitor);
    if (vmode) {
      const auto scale_x = 0.75 * static_cast<double>(vmode->width) / static_cast<double>(width);
      const auto scale_y = 0.75 * static_cast<double>(vmode->height) / static_cast<double>(height);
      const auto scale = static_cast<int>(floor(std::min(scale_x, scale_y)));
      if (scale > 1) {
        return scale;
      }
    }
  }
  return 1;
}

void read_bin_file(const char* file_name, ram_t& ram, const uint32_t start_addr) {
  std::ifstream f(file_name, std::fstream::in | std::fstream::binary);
  if (f.bad()) {
    throw std::runtime_error("Unable to open the binary file.");
  }

  // Read blocks from the file into RAM.
  uint32_t current_addr = start_addr;
  uint32_t total_bytes_read = 0u;
  while (f.good()) {
    uint8_t byte;
    f.read(reinterpret_cast<char*>(&byte), 1);
    ram.store8(current_addr, byte);
    const uint32_t bytes_read = f ? 1 : static_cast<uint32_t>(f.gcount());
    total_bytes_read += bytes_read;
    current_addr += bytes_read;
  }

  f.close();
  if (config_t::instance().verbose()) {
    std::cout << "Read " << total_bytes_read << " bytes from " << file_name << " into RAM @ 0x"
              << std::hex << std::setw(8) << std::setfill('0') << start_addr << "\n";
    std::cout << std::resetiosflags(std::ios::hex);
  }
}

uint32_t read_executable_file(const char* file_name, ram_t& ram, const uint32_t start_addr) {
  // First try to load the file as an ELF32 file.
  elf32::info_t info;
  if (elf32::load(file_name, ram, info) == elf32::status_t::OK) {
    return info.text_address;
  }

  // Otherwise load the file as a raw binary file.
  read_bin_file(file_name, ram, start_addr);
  return start_addr;
}

uint64_t str_to_uint64(const char* str) {
  return static_cast<uint64_t>(std::stoull(std::string(str), nullptr, 0));
}

int64_t str_to_int64(const char* str) {
  return static_cast<int64_t>(str_to_uint64(str));
}

uint32_t str_to_uint32(const char* str) {
  return static_cast<uint32_t>(str_to_uint64(str));
}

void set_simulator_args(ram_t& ram, const int argc, const char** argv) {
  ram.store32(SIM_ARGS_START, argc);
  uint32_t argv_addr = SIM_ARGS_START + 4;
  uint32_t str_addr = argv_addr + argc * 4;
  for (auto k = 0; k < argc; ++k) {
    // Set one argv string pointer.
    ram.store32(argv_addr, str_addr);
    argv_addr += 4;

    // Copy one argument string.
    for (int i = 0;; ++i) {
      if (str_addr >= SIM_ARGS_END) {
        throw std::runtime_error("Too many and too long program arguments.");
      }

      const auto c = argv[k][i];
      ram.store8(str_addr, c);
      ++str_addr;
      if (c == 0) {
        break;
      }
    }
  }
}

void print_help(const char* prg_name) {
  std::cout << "mr32sim - An MRISC32 CPU simulator\n";
  std::cout << "\n";
  std::cout << "Usage: " << prg_name << " [options] program [arguments]\n";
  std::cout << "\n";
  std::cout << "The program can either be an ELF32 executable file or a raw binary file (e.g.\n";
  std::cout << "produced by objcopy -O binary).\n";
  std::cout << "\n";
  std::cout << "Options:\n";
  std::cout << "  -h, --help                       Display this information.\n";
  std::cout << "  -v, --verbose                    Print stats.\n";
  std::cout << "  -g, --gfx                        Enable graphics.\n";
  std::cout << "  -ga ADDR, --gfx-addr ADDR        Set framebuffer address.\n";
  std::cout << "  -gp ADDR, --gfx-palette ADDR     Set palette address.\n";
  std::cout << "  -gw WIDTH, --gfx-width WIDTH     Set framebuffer width.\n";
  std::cout << "  -gh HEIGHT, --gfx-height HEIGHT  Set framebuffer height.\n";
  std::cout << "  -gd DEPTH, --gfx-depth DEPTH     Set framebuffer depht.\n";
  std::cout << "  -f, --fullscreen                 Use fullscreen video mode.\n";
  std::cout << "  --no-scale                       Don't scale window size.\n";
  std::cout << "  -nc, --no-auto-close             Don't auto-close window on exit().\n";
  std::cout << "  -t FILE, --trace FILE            Enable debug trace.\n";
  std::cout << "  -R N, --ram-size N               Set the RAM size (in bytes).\n";
  std::cout << "  -A ADDR, --addr ADDR             Set the program (ROM) start address.\n";
  std::cout << "  -c CYCLES, --cycles CYCLES       Maximum number of CPU cycles to simulate.\n";
  std::cout << "  -P FILE, --perf-syms FILE        Do perf counting using symbols in FILE.\n";
  std::cout << "\n";
  std::cout << "Additional arguments are passed to the simulated program.\n";
  return;
}
}  // namespace

int main(const int argc, const char** argv) {
  // Parse command line options.
  const auto* bin_file = static_cast<const char*>(0);
  uint32_t bin_addr = 0x00000200u;
  int64_t max_cycles = -1;
  std::string perf_syms_file;
  bool fullscreen = false;
  bool scale_window = true;
  int first_sim_argno = 0;
  try {
    for (int k = 1; k < argc; ++k) {
      if (argv[k][0] == '-') {
        if ((std::strcmp(argv[k], "--help") == 0) || (std::strcmp(argv[k], "-h") == 0) ||
            (std::strcmp(argv[k], "-?") == 0)) {
          print_help(argv[0]);
          exit(0);
        } else if ((std::strcmp(argv[k], "-v") == 0) || (std::strcmp(argv[k], "--verbose") == 0)) {
          config_t::instance().set_verbose(true);
        } else if ((std::strcmp(argv[k], "-g") == 0) || (std::strcmp(argv[k], "--gfx") == 0)) {
          config_t::instance().set_gfx_enabled(true);
        } else if ((std::strcmp(argv[k], "-ga") == 0) ||
                   (std::strcmp(argv[k], "--gfx-addr") == 0)) {
          if (k >= (argc - 1)) {
            std::cerr << "Missing option for " << argv[k] << "\n";
            print_help(argv[0]);
            exit(1);
          }
          config_t::instance().set_gfx_addr(str_to_uint32(argv[++k]));
        } else if ((std::strcmp(argv[k], "-gp") == 0) ||
                   (std::strcmp(argv[k], "--gfx-palette") == 0)) {
          if (k >= (argc - 1)) {
            std::cerr << "Missing option for " << argv[k] << "\n";
            print_help(argv[0]);
            exit(1);
          }
          config_t::instance().set_gfx_pal_addr(str_to_uint32(argv[++k]));
        } else if ((std::strcmp(argv[k], "-gw") == 0) ||
                   (std::strcmp(argv[k], "--gfx-width") == 0)) {
          if (k >= (argc - 1)) {
            std::cerr << "Missing option for " << argv[k] << "\n";
            print_help(argv[0]);
            exit(1);
          }
          config_t::instance().set_gfx_width(str_to_uint32(argv[++k]));
        } else if ((std::strcmp(argv[k], "-gh") == 0) ||
                   (std::strcmp(argv[k], "--gfx-height") == 0)) {
          if (k >= (argc - 1)) {
            std::cerr << "Missing option for " << argv[k] << "\n";
            print_help(argv[0]);
            exit(1);
          }
          config_t::instance().set_gfx_height(str_to_uint32(argv[++k]));
        } else if ((std::strcmp(argv[k], "-gd") == 0) ||
                   (std::strcmp(argv[k], "--gfx-depth") == 0)) {
          if (k >= (argc - 1)) {
            std::cerr << "Missing option for " << argv[k] << "\n";
            print_help(argv[0]);
            exit(1);
          }
          config_t::instance().set_gfx_depth(str_to_uint32(argv[++k]));
        } else if ((std::strcmp(argv[k], "-f") == 0) ||
                   (std::strcmp(argv[k], "--fullscreen") == 0)) {
          fullscreen = true;
        } else if (std::strcmp(argv[k], "--no-scale") == 0) {
          scale_window = false;
        } else if ((std::strcmp(argv[k], "-nc") == 0) ||
                   (std::strcmp(argv[k], "--no-auto-close") == 0)) {
          config_t::instance().set_auto_close(false);
        } else if ((std::strcmp(argv[k], "-t") == 0) || (std::strcmp(argv[k], "--trace") == 0)) {
          if (k >= (argc - 1)) {
            std::cerr << "Missing option for " << argv[k] << "\n";
            print_help(argv[0]);
            exit(1);
          }
          config_t::instance().set_trace_file_name(std::string(argv[++k]));
          config_t::instance().set_trace_enabled(true);
        } else if ((std::strcmp(argv[k], "-R") == 0) || (std::strcmp(argv[k], "--ram-size") == 0)) {
          if (k >= (argc - 1)) {
            std::cerr << "Missing option for " << argv[k] << "\n";
            print_help(argv[0]);
            exit(1);
          }
          config_t::instance().set_ram_size(str_to_uint64(argv[++k]));
        } else if ((std::strcmp(argv[k], "-A") == 0) || (std::strcmp(argv[k], "--addr") == 0)) {
          if (k >= (argc - 1)) {
            std::cerr << "Missing option for " << argv[k] << "\n";
            print_help(argv[0]);
            exit(1);
          }
          bin_addr = str_to_uint32(argv[++k]);
        } else if ((std::strcmp(argv[k], "-c") == 0) || (std::strcmp(argv[k], "--cycles") == 0)) {
          if (k >= (argc - 1)) {
            std::cerr << "Missing option for " << argv[k] << "\n";
            print_help(argv[0]);
            exit(1);
          }
          max_cycles = str_to_int64(argv[++k]);
        } else if ((std::strcmp(argv[k], "-P") == 0) ||
                   (std::strcmp(argv[k], "--perf-syms") == 0)) {
          if (k >= (argc - 1)) {
            std::cerr << "Missing option for " << argv[k] << "\n";
            print_help(argv[0]);
            exit(1);
          }
          perf_syms_file = std::string(argv[++k]);
          config_t::instance().set_verbose(true);
        } else {
          std::cerr << "Error: Unknown option: " << argv[k] << "\n";
          print_help(argv[0]);
          exit(1);
        }
      } else if (bin_file == static_cast<const char*>(0)) {
        bin_file = argv[k];
        first_sim_argno = k;
        break;
      } else {
        std::cerr << "Error: Only a single program file can be loaded.\n";
        print_help(argv[0]);
        exit(1);
      }
    }
  } catch (...) {
    std::cerr << "Error: Couldn't parse command line arguments.\n";
    print_help(argv[0]);
    exit(1);
  }
  if (bin_file == static_cast<const char*>(0)) {
    std::cerr << "Error: No program file specified.\n";
    print_help(argv[0]);
    std::exit(1);
  }

  try {
    // Initialize the RAM.
    ram_t ram(config_t::instance().ram_size());
    s_ram = &ram;

    // Initialize simulator program arguments.
    const char** sim_argv = &argv[first_sim_argno];
    const int sim_argc = argc - first_sim_argno;
    set_simulator_args(ram, sim_argc, sim_argv);

    // Initialize the perf symbols.
    perf_symbols_t perf_symbols;
    if (!perf_syms_file.empty()) {
      perf_symbols.load(perf_syms_file);
    }

    // Load the program file into RAM.
    auto start_addr = read_executable_file(bin_file, ram, bin_addr);
    // TODO(m): Optionally override the CPU RESET_PC with start_addr.
    (void)start_addr;

    // HACK: Populate MMIO memory with MC1 fields.
    const uint32_t MMIO_START = 0xc0000000u;
    if (config_t::instance().ram_size() >= (MMIO_START + 64)) {
      ram.store32(MMIO_START + 8, 70000000);     // CPUCLK
      ram.store32(MMIO_START + 12, 128 * 1024);  // VRAMSIZE
      ram.store32(MMIO_START + 20, 1920);        // VIDWIDTH
      ram.store32(MMIO_START + 24, 1080);        // VIDHEIGHT
      ram.store32(MMIO_START + 28, 60 * 65536);  // VIDFPS
      ram.store32(MMIO_START + 40, 4);           // SWITCHES
    }

    // Initialize the CPU.
    cpu_simple_t cpu(ram, perf_symbols);

    if (config_t::instance().verbose()) {
      std::cout << "------------------------------------------------------------------------\n";
    }

    // Run the CPU in a separate thread.
    std::atomic_bool cpu_done(false);
    uint32_t cpu_exit_code = 0u;
    std::thread cpu_thread([&cpu_exit_code, &cpu, &cpu_done, max_cycles] {
      try {
        // Run until the program returns.
        cpu_exit_code = cpu.run(max_cycles);
      } catch (std::exception& e) {
        std::cerr << "Exception in CPU thread: " << e.what() << "\n";
        cpu_exit_code = 1u;
      }
      cpu_done = true;
    });

    if (config_t::instance().gfx_enabled()) {
      try {
        // Initialize GLFW.
        if (glfwInit() != GLFW_TRUE) {
          throw std::runtime_error("Unable to initialize GLFW.");
        }

        // We want the display to be 24-bit RGB.
        glfwWindowHint(GLFW_RED_BITS, 8);
        glfwWindowHint(GLFW_GREEN_BITS, 8);
        glfwWindowHint(GLFW_BLUE_BITS, 8);
        glfwWindowHint(GLFW_ALPHA_BITS, GLFW_DONT_CARE);
        glfwWindowHint(GLFW_DEPTH_BITS, GLFW_DONT_CARE);
        glfwWindowHint(GLFW_STENCIL_BITS, GLFW_DONT_CARE);

        // The GL context should support the 3.2 core profile (forward compatible).
        // This ensures that we get a modern GL context on macOS.
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
        glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

        // Create a GLFW window.
        uint32_t window_width;
        uint32_t window_height;
        int window_scale;
        GLFWmonitor* monitor = nullptr;
        if (fullscreen) {
          monitor = glfwGetPrimaryMonitor();
          const GLFWvidmode* mode = glfwGetVideoMode(monitor);
          window_width = mode->width;
          window_height = mode->height;
          window_scale = 1;
        } else {
          window_width = config_t::instance().gfx_width();
          window_height = config_t::instance().gfx_height();
          if (scale_window) {
            window_scale = adaptive_window_scale(nullptr, window_width, window_height);
          } else {
            window_scale = 1;
          }
        }
        auto* window = glfwCreateWindow(static_cast<int>(window_width) * window_scale,
                                        static_cast<int>(window_height) * window_scale,
                                        "MRISC32 Simulator",
                                        monitor,
                                        nullptr);
        if (window != nullptr) {
          if (fullscreen) {
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
          }

          glfwMakeContextCurrent(window);

          // Initialize GLAD.
          if (gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress)) == 0) {
            glfwDestroyWindow(window);
            glfwTerminate();
            throw std::runtime_error("Unable to initialize GLAD.");
          }
          if (config_t::instance().verbose()) {
            std::cerr << "OpenGL version: " << GLVersion.major << "." << GLVersion.minor << "\n";
          }

          // Set up event handlers.
          glfwSetKeyCallback(window, keyhandler);
          glfwSetCursorPosCallback(window, mousehandler);
          glfwSetMouseButtonCallback(window, mousebtnhandler);

          // Init the "GPU".
          gpu_t gpu(ram);

          // Enable vsync.
          glfwSwapInterval(1);

          // Main loop.
          bool simulation_finished = false;
          uint32_t frame_no = 0;
          while (!glfwWindowShouldClose(window)) {
            // Update the video mode.
            gpu.configure();
            if (!fullscreen && (window_width != gpu.width() || window_height != gpu.height())) {
              window_width = gpu.width();
              window_height = gpu.height();
              if (scale_window) {
                window_scale = adaptive_window_scale(window, window_width, window_height);
              }
              glfwSetWindowSize(window,
                                static_cast<int>(window_width) * window_scale,
                                static_cast<int>(window_height) * window_scale);
            }

            // Update the frame number (MC1 compat).
            ram.store32(0xc0000020, frame_no);
            frame_no += 1u;

            // Get the actual window framebuffer size (note: this is important on systems that use
            // coordinate scaling, such as on macos with retina display).
            int actual_fb_width;
            int actual_fb_height;
            glfwGetFramebufferSize(window, &actual_fb_width, &actual_fb_height);

            // Paint the CPU RAM framebuffer contents to the window.
            gpu.paint(actual_fb_width, actual_fb_height);

            // Swap front/back buffers and poll window events.
            glfwSwapBuffers(window);
            glfwPollEvents();

            // Simulation finished?
            if (cpu_done && !simulation_finished) {
              if (config_t::instance().auto_close()) {
                glfwSetWindowShouldClose(window, GLFW_TRUE);
              } else {
                glfwSetWindowTitle(window, "MRISC32 Simulator - *Finished*");
              }
              simulation_finished = true;
            }
          }

          // Clean up GPU resources before we close the window.
          gpu.cleanup();

          // Close the window.
          glfwDestroyWindow(window);
          glfwTerminate();
        }
      } catch (std::exception& e) {
        std::cerr << "Graphics error: " << e.what() << "\n";
      }

      cpu.terminate();
    }

    // Wait for the cpu thread to finish.
    cpu_thread.join();
    const int exit_code = static_cast<int>(cpu_exit_code);

    if (config_t::instance().verbose()) {
      // Show some stats.
      std::cout << "------------------------------------------------------------------------\n";
      std::cout << "Exit code: " << exit_code << "\n";
      cpu.dump_stats();

      // Dump perf stats.
      if (perf_symbols.has_symbols()) {
        std::cout << "\n";
        perf_symbols.print();
      }
    }

    // Dump some RAM (we use the same range as the MC1 VRAM).
    // TODO(m): Control this with command line arguments.
    cpu.dump_ram(0x40000000u, 0x40040000u, "/tmp/mrisc32_sim_vram.bin");

    std::exit(exit_code);
  } catch (std::exception& e) {
    std::cerr << "Error: " << e.what() << "\n";
    std::exit(1);
  }
}

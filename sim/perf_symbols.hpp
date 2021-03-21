//--------------------------------------------------------------------------------------------------
// Copyright (c) 2021 Marcus Geelnard
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

#ifndef SIM_PERF_SYMBOLS_HPP_
#define SIM_PERF_SYMBOLS_HPP_

#include <cstdint>
#include <string>
#include <vector>

class perf_symbols_t {
public:
  struct symbol_t {
    symbol_t(const uint32_t addr_, const std::string& name_) : name(name_), addr(addr_) {
    }
    uint64_t cycles = 0;  ///< Number of cycles spent in this function.
    std::string name;     ///< Name of the function.
    uint32_t addr;        ///< Starting (call) address of the function.
  };

  perf_symbols_t() {
  }

  bool has_symbols() const {
    return !m_symbols.empty();
  }

  void load(const std::string& file_name);

  void print() const;

  void add_ref(const uint32_t addr) {
    if (m_symbols.empty()) {
      return;
    }

    // This instruction is very likely to be in the same function as the previous instruction.
    if (m_symbols[m_last_sym_idx].addr <= addr && addr <= m_symbols[m_last_sym_idx + 1].addr) {
      ++m_symbols[m_last_sym_idx].cycles;
      return;
    }

    // Use binary search to find the symbol.
    const auto n = static_cast<int>(m_symbols.size());
    int L = 0;
    auto R = n - 2;
    while (L <= R) {
      int m = (L + R) >> 1;
      if (m_symbols[m + 1].addr <= addr) {
        L = m + 1;
      } else if (m_symbols[m].addr > addr) {
        R = m - 1;
      } else {
        m_last_sym_idx = m;
        ++m_symbols[m].cycles;
        break;
      }
    }
  }

private:
  // List of symbols, sorted by address.
  std::vector<symbol_t> m_symbols;

  // Simple temporal acceleration (assumes single threaded calls to add_ref).
  int m_last_sym_idx = 0;
};

#endif // SIM_PERF_SYMBOLS_HPP_

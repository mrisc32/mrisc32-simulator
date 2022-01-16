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

#include "perf_symbols.hpp"

#include <algorithm>
#include <cstdio>
#include <fstream>

namespace {
perf_symbols_t::symbol_t parse_symbol(const std::string& str) {
  const uint32_t addr = std::stoul(str.substr(0, 8), 0, 16);
  const auto name = str.substr(9);
  return perf_symbols_t::symbol_t(addr, name);
}

bool addr_lt(const perf_symbols_t::symbol_t& a, const perf_symbols_t::symbol_t& b) {
  return a.addr < b.addr;
}

bool cycles_gt(const perf_symbols_t::symbol_t& a, const perf_symbols_t::symbol_t& b) {
  return a.cycles > b.cycles;
}
}  // namespace

void perf_symbols_t::load(const std::string& file_name) {
  {
    // Load the file.
    std::ifstream file(file_name);
    std::string str;
    while (std::getline(file, str)) {
      m_symbols.emplace_back(parse_symbol(str));
    }
  }

  // Add first & last elements (to get complete the ranges).
  m_symbols.emplace_back(perf_symbols_t::symbol_t(0x00000000u, "<NULL>"));
  m_symbols.emplace_back(perf_symbols_t::symbol_t(0xffffffffu, "<END>"));

  // Sort the symbol table (increasing addresses).
  std::sort(m_symbols.begin(), m_symbols.end(), addr_lt);

  m_has_symbols = !m_symbols.empty();
}

void perf_symbols_t::print() const {
  // Sort the symbol (decreasing cycle counts).
  auto syms = m_symbols;
  std::sort(syms.begin(), syms.end(), cycles_gt);

  // Print symbols & cycles.
  printf("Address (hex)\tCycles\tFunction\n");
  for (const auto& sym : syms) {
    if (sym.cycles > 0u) {
      printf("0x%08x\t%ld\t%s\n", sym.addr, sym.cycles, sym.name.c_str());
    }
  }
}

void perf_symbols_t::add_ref_impl(const uint32_t addr) {
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

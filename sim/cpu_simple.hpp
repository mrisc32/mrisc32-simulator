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

#ifndef SIM_CPU_SIMPLE_HPP_
#define SIM_CPU_SIMPLE_HPP_

#include "cpu.hpp"

/// @brief A simple implementation of a CPU core.
///
/// This implementation is not pipelined. It executes each instruction in a single CPU cycle.
class cpu_simple_t : public cpu_t {
public:
  /// @brief Constructor for cpu_simple_t.
  ///
  /// @param ram The RAM to use for this CPU instance.
  /// @param perf_symbols Performance symbols for profiling.
  cpu_simple_t(ram_t& ram, perf_symbols_t& perf_symbols);

  uint32_t run(uint32_t start_addr, int64_t max_cycles) override;

private:
  uint32_t xchgsr(uint32_t a, uint32_t b, bool a_is_z_reg);
  void update_mc1_clkcnt();

  uint32_t* m_mc1_mmio;
};

#endif  // SIM_CPU_SIMPLE_HPP_

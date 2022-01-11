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

#include "cpu.hpp"

#include "config.hpp"

#include <algorithm>
#include <cstdio>
#include <iomanip>
#include <iostream>

#ifdef __x86_64__
#include <pmmintrin.h>
#include <xmmintrin.h>
#endif  // __x86_64__

namespace {

void configure_fpu() {
#ifdef __x86_64__
  _MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_ON);
  _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON);
#endif  // __x86_64__
}

}  // namespace

cpu_t::cpu_t(ram_t& ram, perf_symbols_t& perf_symbols)
    : m_ram(ram), m_perf_symbols(perf_symbols), m_syscalls(ram) {
  if (config_t::instance().trace_enabled()) {
    m_trace_file.open(config_t::instance().trace_file_name(), std::ios::out | std::ios::binary);
    m_enable_tracing = true;
  }
  reset();
}

cpu_t::~cpu_t() {
  if (m_trace_file.is_open()) {
    flush_debug_trace_buffer();
    m_trace_file.close();
  }
}

void cpu_t::reset() {
  // Clear registers.
  std::fill(m_regs.begin(), m_regs.end(), 0u);
  for (auto reg = m_vregs.begin(); reg != m_vregs.end(); ++reg) {
    std::fill(reg->begin(), reg->end(), 0u);
  }

  // Clear run state.
  m_syscalls.clear();
  m_terminate_requested = false;

  // Configure the host FPU to match MRISC32 behavior.
  configure_fpu();
}

void cpu_t::terminate() {
  m_terminate_requested = true;
}

void cpu_t::dump_stats() {
  const auto dt_us =
      std::chrono::duration_cast<std::chrono::microseconds>(m_stop_time - m_start_time).count();
  const auto running_time_s = static_cast<double>(dt_us) * 0.000001;
  const auto mops =
      0.000001 * static_cast<double>(m_total_cycle_count) / static_cast<double>(running_time_s);
  std::cout << "CPU instructions:\n";
  std::cout << " Fetched instructions: " << m_fetched_instr_count << "\n";
  std::cout << " Vector loops:         " << m_vector_loop_count << "\n";
  std::cout << " Total CPU cycles:     " << m_total_cycle_count << "\n";
  std::cout << " Mcycles/s:            " << mops << "\n";
}

void cpu_t::dump_ram(const uint32_t begin, const uint32_t end, const std::string& file_name) {
  std::ofstream file;
  file.open(file_name, std::ios::out | std::ios::binary);
  for (uint32_t addr = begin; addr < end; ++addr) {
    const uint8_t byte = static_cast<uint8_t>(m_ram.load8(addr));
    file.write(reinterpret_cast<const char*>(&byte), 1);
  }
  file.close();
}

void cpu_t::append_debug_trace_impl(const debug_trace_t& trace) {
  // Pick the next entry into the trace buffer.
  uint8_t* entry = &m_debug_trace_buf[m_debug_trace_file_buf_entries * TRACE_ENTRY_SIZE];
  ++m_debug_trace_file_buf_entries;

  const uint32_t flags = (trace.valid ? 1 : 0) | (trace.src_a_valid ? 2 : 0) |
                         (trace.src_b_valid ? 4 : 0) | (trace.src_c_valid ? 8 : 0);
  entry[0] = static_cast<uint8_t>(flags);
  entry[1] = static_cast<uint8_t>(flags >> 8);
  entry[2] = static_cast<uint8_t>(flags >> 16);
  entry[3] = static_cast<uint8_t>(flags >> 24);

  entry[4] = static_cast<uint8_t>(trace.pc);
  entry[5] = static_cast<uint8_t>(trace.pc >> 8);
  entry[6] = static_cast<uint8_t>(trace.pc >> 16);
  entry[7] = static_cast<uint8_t>(trace.pc >> 24);

  if (trace.src_a_valid) {
    entry[8] = static_cast<uint8_t>(trace.src_a);
    entry[9] = static_cast<uint8_t>(trace.src_a >> 8);
    entry[10] = static_cast<uint8_t>(trace.src_a >> 16);
    entry[11] = static_cast<uint8_t>(trace.src_a >> 24);
  }
  if (trace.src_b_valid) {
    entry[12] = static_cast<uint8_t>(trace.src_b);
    entry[13] = static_cast<uint8_t>(trace.src_b >> 8);
    entry[14] = static_cast<uint8_t>(trace.src_b >> 16);
    entry[15] = static_cast<uint8_t>(trace.src_b >> 24);
  }
  if (trace.src_c_valid) {
    entry[16] = static_cast<uint8_t>(trace.src_c);
    entry[17] = static_cast<uint8_t>(trace.src_c >> 8);
    entry[18] = static_cast<uint8_t>(trace.src_c >> 16);
    entry[19] = static_cast<uint8_t>(trace.src_c >> 24);
  }

  // Time to flush the trace buffer?
  if (m_debug_trace_file_buf_entries >= TRACE_FLUSH_INTERVAL) {
    flush_debug_trace_buffer();
  }
}

void cpu_t::flush_debug_trace_buffer() {
  if (m_debug_trace_file_buf_entries > 0) {
    const auto num_bytes = m_debug_trace_file_buf_entries * TRACE_ENTRY_SIZE;
    m_trace_file.write(reinterpret_cast<const char*>(&m_debug_trace_buf[0]), num_bytes);
    m_trace_file.flush();
    m_debug_trace_file_buf_entries = 0;
  }
}

void cpu_t::begin_simulation() {
  m_start_time = std::chrono::high_resolution_clock::now();
}

void cpu_t::end_simulation() {
  m_stop_time = std::chrono::high_resolution_clock::now();
}

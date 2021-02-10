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

#ifndef SIM_CPU_HPP_
#define SIM_CPU_HPP_

#include "ram.hpp"
#include "syscalls.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <fstream>

/// @brief A CPU core instance.
class cpu_t {
public:
  virtual ~cpu_t();

  /// @brief Reset the CPU state.
  void reset();

  /// @brief Terminate the CPU execution (can be called from another thread).
  void terminate();

  /// @brief Start running code at a given memory address.
  /// @param max_cycles The maximum number of cycles to simulate (-1 = no limit).
  /// @returns The program return code (the argument to exit()).
  virtual uint32_t run(const int64_t max_cycles) = 0;

  /// @brief Dump CPU stats from the last run.
  void dump_stats();

  /// @brief Dump RAM contents.
  void dump_ram(const uint32_t begin, const uint32_t end, const std::string& file_name);

protected:
  // This constructor is called from derived classes.
  cpu_t(ram_t& ram);

  // Register configuration.
  static const uint32_t NUM_REGS = 32u;
  static const uint32_t LOG2_NUM_VECTOR_ELEMENTS = 4u;  // Must be at least 4
  static const uint32_t NUM_VECTOR_ELEMENTS = 1u << LOG2_NUM_VECTOR_ELEMENTS;
  static const uint32_t NUM_VECTOR_REGS = 32u;

  // Reset start address.
  static const uint32_t RESET_PC = 0x00000200u;

  // Named registers.
  static const uint32_t REG_Z = 0u;
  static const uint32_t REG_FP = 26u;
  static const uint32_t REG_TP = 27u;
  static const uint32_t REG_SP = 28u;
  static const uint32_t REG_VL = 29u;
  static const uint32_t REG_LR = 30u;
  static const uint32_t REG_PC = 31u;

  // EX operations.
  static const uint32_t EX_OP_CPUID = 0x00u;

  static const uint32_t EX_OP_LDHI = 0x01u;
  static const uint32_t EX_OP_ADDPCHI = 0x02u;

  static const uint32_t EX_OP_AND = 0x10u;
  static const uint32_t EX_OP_OR = 0x11u;
  static const uint32_t EX_OP_XOR = 0x12u;
  static const uint32_t EX_OP_ASR = 0x13u;
  static const uint32_t EX_OP_LSL = 0x14u;
  static const uint32_t EX_OP_LSR = 0x15u;

  static const uint32_t EX_OP_ADD = 0x16u;
  static const uint32_t EX_OP_SUB = 0x17u;
  static const uint32_t EX_OP_MIN = 0x18u;
  static const uint32_t EX_OP_MAX = 0x19u;
  static const uint32_t EX_OP_MINU = 0x1au;
  static const uint32_t EX_OP_MAXU = 0x1bu;
  static const uint32_t EX_OP_SEL = 0x1cu;
  static const uint32_t EX_OP_SHUF = 0x1du;

  static const uint32_t EX_OP_SEQ = 0x1eu;
  static const uint32_t EX_OP_SNE = 0x1fu;
  static const uint32_t EX_OP_SLT = 0x20u;
  static const uint32_t EX_OP_SLTU = 0x21u;
  static const uint32_t EX_OP_SLE = 0x22u;
  static const uint32_t EX_OP_SLEU = 0x23u;

  static const uint32_t EX_OP_DIV = 0x30u;
  static const uint32_t EX_OP_DIVU = 0x31u;
  static const uint32_t EX_OP_REM = 0x32u;
  static const uint32_t EX_OP_REMU = 0x33u;

  static const uint32_t EX_OP_MUL = 0x34u;
  static const uint32_t EX_OP_MULHI = 0x35u;
  static const uint32_t EX_OP_MULHIU = 0x36u;
  static const uint32_t EX_OP_MULQ = 0x37u;
  static const uint32_t EX_OP_MULQR = 0x38u;

  static const uint32_t EX_OP_PACK = 0x3au;
  static const uint32_t EX_OP_PACKS = 0x3bu;
  static const uint32_t EX_OP_PACKSU = 0x3cu;

  static const uint32_t EX_OP_FMIN = 0x40u;
  static const uint32_t EX_OP_FMAX = 0x41u;
  static const uint32_t EX_OP_FSEQ = 0x42u;
  static const uint32_t EX_OP_FSNE = 0x43u;
  static const uint32_t EX_OP_FSLT = 0x44u;
  static const uint32_t EX_OP_FSLE = 0x45u;
  static const uint32_t EX_OP_FSUNORD = 0x46u;
  static const uint32_t EX_OP_FSORD = 0x47u;

  static const uint32_t EX_OP_ITOF = 0x48u;
  static const uint32_t EX_OP_UTOF = 0x49u;
  static const uint32_t EX_OP_FTOI = 0x4au;
  static const uint32_t EX_OP_FTOU = 0x4bu;
  static const uint32_t EX_OP_FTOIR = 0x4cu;
  static const uint32_t EX_OP_FTOUR = 0x4du;
  static const uint32_t EX_OP_FPACK = 0x4eu;

  static const uint32_t EX_OP_FADD = 0x50u;
  static const uint32_t EX_OP_FSUB = 0x51u;
  static const uint32_t EX_OP_FMUL = 0x52u;
  static const uint32_t EX_OP_FDIV = 0x53u;

  static const uint32_t EX_OP_ADDS = 0x60u;
  static const uint32_t EX_OP_ADDSU = 0x61u;
  static const uint32_t EX_OP_ADDH = 0x62u;
  static const uint32_t EX_OP_ADDHU = 0x63u;
  static const uint32_t EX_OP_ADDHR = 0x64u;
  static const uint32_t EX_OP_ADDHUR = 0x65u;
  static const uint32_t EX_OP_SUBS = 0x66u;
  static const uint32_t EX_OP_SUBSU = 0x67u;
  static const uint32_t EX_OP_SUBH = 0x68u;
  static const uint32_t EX_OP_SUBHU = 0x69u;
  static const uint32_t EX_OP_SUBHR = 0x6au;
  static const uint32_t EX_OP_SUBHUR = 0x6bu;

  // Two-operand type B operations.
  static const uint32_t EX_OP_REV = 0x007cu;
  static const uint32_t EX_OP_CLZ = 0x017cu;
  static const uint32_t EX_OP_POPCNT = 0x027cu;

  static const uint32_t EX_OP_FUNPL = 0x007du;
  static const uint32_t EX_OP_FUNPH = 0x017du;
  static const uint32_t EX_OP_FSQRT = 0x087du;

  // Memory operations.
  static const uint32_t MEM_OP_NONE = 0x0u;
  static const uint32_t MEM_OP_LOAD8 = 0x1u;
  static const uint32_t MEM_OP_LOAD16 = 0x2u;
  static const uint32_t MEM_OP_LOAD32 = 0x3u;
  static const uint32_t MEM_OP_LOADU8 = 0x5u;
  static const uint32_t MEM_OP_LOADU16 = 0x6u;
  static const uint32_t MEM_OP_LDEA = 0x07u;
  static const uint32_t MEM_OP_STORE8 = 0x9u;
  static const uint32_t MEM_OP_STORE16 = 0xau;
  static const uint32_t MEM_OP_STORE32 = 0xbu;

  // Packed operation modes.
  static const uint32_t PACKED_NONE = 0u;
  static const uint32_t PACKED_BYTE = 1u;
  static const uint32_t PACKED_HALF_WORD = 2u;

  // One vector register.
  using vreg_t = std::array<uint32_t, NUM_VECTOR_ELEMENTS>;

  // Debug trace struct.
  struct debug_trace_t {
    bool valid;
    bool src_a_valid;
    bool src_b_valid;
    bool src_c_valid;
    uint32_t pc;
    uint32_t src_a;
    uint32_t src_b;
    uint32_t src_c;
  };

  /// @brief Append a single debug trace record to the trace file.
  /// @param trace The trace record.
  void append_debug_trace(const debug_trace_t& trace);

  // Debug trace file.
  std::ofstream m_trace_file;

  // Memory interface.
  ram_t& m_ram;

  // Syscalls interface.
  syscalls_t m_syscalls;

  // Scalar registers.
  std::array<uint32_t, NUM_REGS> m_regs;

  // Vector registers.
  std::array<vreg_t, NUM_VECTOR_REGS> m_vregs;

  // Run stats.
  uint32_t m_fetched_instr_count;
  uint32_t m_vector_loop_count;
  uint32_t m_total_cycle_count;

  std::atomic_bool m_terminate_requested;
};

#endif  // SIM_CPU_HPP_

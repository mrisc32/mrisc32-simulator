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

#include "cpu_simple.hpp"

#include "packed_float.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <exception>

namespace {
struct reg_id_t {
  uint32_t no;
  bool is_vector;
};

struct decode_t {
  uint32_t src_imm;
  bool src_b_is_imm;
  bool src_b_is_stride;

  reg_id_t src_reg_a;
  reg_id_t src_reg_b;
  reg_id_t src_reg_c;
  reg_id_t dst_reg;

  uint32_t ex_op;        // EX operation.
  uint32_t packed_mode;  // Packed operation mode.

  uint32_t mem_op;  // MEM operation.
};

struct vector_state_t {
  uint32_t vector_len;   // Actual vector length.
  uint32_t idx;          // Current vector index.
  uint32_t stride;       // Stride for vector memory address calculations.
  uint32_t addr_offset;  // Current address offset (incremented by load/store stride).
  bool is_vector_op;     // True if this is vector op.
  bool folding;          // True if this is a folding vector op.
};

inline uint32_t decode_imm15(const uint32_t iword) {
  const auto op_high = iword >> (32 - 6);
  if (op_high >= 0x01U && op_high <= 0x0fU) {
    // Immediate encoding = I15 (i.e. format C load/store).
    return (iword & 0x00007fffu) | ((iword & 0x00004000u) ? 0xffff8000u : 0u);
  } else {
    // Immediate encoding = I15HL.
    if ((iword & 0x00004000u) != 0u) {
      // H-bit == 1 => Place immediate value in upper 14 bits.
      return ((iword & 0x00003fffu) << 18) | ((iword & 1) ? 0x0003ffffu : 0u);
    } else {
      // H-bit == 0 => Place immediate value in lower 14 bits.
      return (iword & 0x00003fffu) | ((iword & 0x00002000u) ? 0xffffc000u : 0u);
    }
  }
}

inline uint32_t decode_imm18(const uint32_t iword) {
  // I18X4
  return ((iword & 0x0003ffffu) << 2) | ((iword & 0x00020000u) ? 0xfff00000u : 0u);
}

inline uint32_t decode_imm21(const uint32_t iword) {
  const auto op = (iword >> (32 - 6)) - 0x30U;
  if (op <= 4) {
    // I21X4
    return ((iword & 0x1fffffu) << 2) | ((iword & 0x100000u) ? 0xff800000u : 0u);
  } else if (op == 5) {
    // I21H
    return (iword & 0x1fffffu) << 11;
  } else {
    // I21HL
    if ((iword & 0x00100000u) != 0u) {
      // H-bit == 1 => Place immediate value in upper 20 bits.
      return ((iword & 0x000fffffu) << 12) | ((iword & 1) ? 0x00000fffu : 0u);
    } else {
      // H-bit == 0 => Place immediate value in lower 20 bits.
      return (iword & 0x000fffffu) | ((iword & 0x00080000u) ? 0xfff00000u : 0u);
    }
  }
}

inline std::string as_hex32(const uint32_t x) {
  char str[16];
  std::snprintf(str, sizeof(str) - 1, "0x%08x", x);
  return std::string(&str[0]);
}

template <typename T>
inline std::string as_dec(const T x) {
  char str[32];
  std::snprintf(str, sizeof(str) - 1, "%d", static_cast<int>(x));
  return std::string(&str[0]);
}

inline uint32_t index_scale_factor(const uint32_t packed_mode) {
  return uint32_t(1u) << packed_mode;
}

inline uint32_t actual_vector_len(const uint32_t requested_length,
                                  const uint32_t num_elements,
                                  const bool fold) {
  const auto l = std::min(requested_length, num_elements);
  return fold ? (l >> 1) : l;
}

inline float as_f32(const uint32_t x) {
  float result;
  std::memcpy(&result, &x, sizeof(float));
  return result;
}

inline uint32_t as_u32(const float x) {
  uint32_t result;
  std::memcpy(&result, &x, sizeof(uint32_t));
  return result;
}

inline uint32_t add32(const uint32_t a, const uint32_t b) {
  return a + b;
}

inline uint32_t add16x2(const uint32_t a, const uint32_t b) {
  const uint32_t hi = (a & 0xffff0000u) + (b & 0xffff0000u);
  const uint32_t lo = (a + b) & 0x0000ffffu;
  return hi | lo;
}

inline uint32_t add8x4(const uint32_t a, const uint32_t b) {
  const uint32_t hi = ((a & 0xff00ff00u) + (b & 0xff00ff00u)) & 0xff00ff00u;
  const uint32_t lo = ((a & 0x00ff00ffu) + (b & 0x00ff00ffu)) & 0x00ff00ffu;
  return hi | lo;
}

inline uint32_t sub32(const uint32_t a, const uint32_t b) {
  return add32((~a) + 1u, b);
}

inline uint32_t sub16x2(const uint32_t a, const uint32_t b) {
  return add16x2(add16x2(~a, 0x00010001u), b);
}

inline uint32_t sub8x4(const uint32_t a, const uint32_t b) {
  return add8x4(add8x4(~a, 0x01010101u), b);
}

inline uint32_t set32(const uint32_t a, const uint32_t b, bool (*cmp)(uint32_t, uint32_t)) {
  return cmp(a, b) ? 0xffffffffu : 0u;
}

inline uint32_t set16x2(const uint32_t a, const uint32_t b, bool (*cmp)(uint16_t, uint16_t)) {
  const uint32_t h1 =
      (cmp(static_cast<uint16_t>(a >> 16), static_cast<uint16_t>(b >> 16)) ? 0xffff0000u : 0u);
  const uint32_t h0 = (cmp(static_cast<uint16_t>(a), static_cast<uint16_t>(b)) ? 0x0000ffffu : 0u);
  return h1 | h0;
}

inline uint32_t set8x4(const uint32_t a, const uint32_t b, bool (*cmp)(uint8_t, uint8_t)) {
  const uint32_t b3 =
      (cmp(static_cast<uint8_t>(a >> 24), static_cast<uint8_t>(b >> 24)) ? 0xff000000u : 0u);
  const uint32_t b2 =
      (cmp(static_cast<uint8_t>(a >> 16), static_cast<uint8_t>(b >> 16)) ? 0x00ff0000u : 0u);
  const uint32_t b1 =
      (cmp(static_cast<uint8_t>(a >> 8), static_cast<uint8_t>(b >> 8)) ? 0x0000ff00u : 0u);
  const uint32_t b0 = (cmp(static_cast<uint8_t>(a), static_cast<uint8_t>(b)) ? 0x000000ffu : 0u);
  return b3 | b2 | b1 | b0;
}

inline uint32_t sel32(const uint32_t a, const uint32_t b, const uint32_t mask) {
  return (a & mask) | (b & ~mask);
}

template <int BITS>
inline uint32_t bf_ctrl_width(const uint32_t ctrl) {
  constexpr int WIDTH_POS = (BITS >= 4) ? 8 : 4;
  auto w = (ctrl >> WIDTH_POS) & ((1u << BITS) - 1);
  return (w == 0) ? (1 << BITS) : w;
}

template <int BITS>
inline uint32_t bf_ctrl_offset(const uint32_t ctrl) {
  return ctrl & ((1 << BITS) - 1);
}

template <int BITS>
inline uint32_t bf_full_mask() {
  switch (BITS) {
    case 3:
      return 0xffu;
    case 4:
      return 0xffffu;
    case 5:
    default:
      return 0xffffffffu;
  }
}

template <int BITS>
inline uint32_t bf_mask(const uint32_t ctrl) {
  auto w = bf_ctrl_width<BITS>(ctrl);
  return (w == (1u << BITS)) ? bf_full_mask<BITS>() : (1u << w) - 1;
}

template <int BITS>
inline uint32_t bf_sign_bit_pos(const uint32_t ctrl) {
  return bf_ctrl_width<BITS>(ctrl) - 1;
}

template <int BITS, typename T>
inline uint32_t bf_extract(const T x, const uint32_t ctrl) {
  const auto y = static_cast<uint32_t>(static_cast<int32_t>(x) >> bf_ctrl_offset<BITS>(ctrl)) &
                 bf_mask<BITS>(ctrl);
  const auto sbit = bf_sign_bit_pos<BITS>(ctrl);
  return ((y & (1u << sbit)) != 0u) ? (y | (~0 << sbit)) & bf_full_mask<BITS>() : y;
}

template <int BITS, typename T>
inline uint32_t bf_extract_u(const T x, const uint32_t ctrl) {
  return (static_cast<uint32_t>(x) >> bf_ctrl_offset<BITS>(ctrl)) & bf_mask<BITS>(ctrl);
}

template <int BITS, typename T>
inline uint32_t bf_make(const T x, const uint32_t ctrl) {
  return ((static_cast<uint32_t>(x) & bf_mask<BITS>(ctrl)) << bf_ctrl_offset<BITS>(ctrl)) &
         bf_full_mask<BITS>();
}

inline uint32_t ebf32(const uint32_t a, const uint32_t b) {
  return bf_extract<5>(a, b);
}

inline uint32_t ebf16x2(const uint32_t a, const uint32_t b) {
  const auto a1 = static_cast<int16_t>(a >> 16);
  const auto a0 = static_cast<int16_t>(a);
  const auto b1 = b >> 16;
  const auto b0 = b & 0xffff;
  const auto c1 = bf_extract<4>(a1, b1);
  const auto c0 = bf_extract<4>(a0, b0);
  return (c1 << 16) | c0;
}

inline uint32_t ebf8x4(const uint32_t a, const uint32_t b) {
  const auto a3 = static_cast<int8_t>(a >> 24);
  const auto a2 = static_cast<int8_t>(a >> 16);
  const auto a1 = static_cast<int8_t>(a >> 8);
  const auto a0 = static_cast<int8_t>(a);
  const auto b3 = b >> 24;
  const auto b2 = (b >> 16) & 0xff;
  const auto b1 = (b >> 8) & 0xff;
  const auto b0 = b & 0xff;
  const auto c3 = bf_extract<3>(a3, b3);
  const auto c2 = bf_extract<3>(a2, b2);
  const auto c1 = bf_extract<3>(a1, b1);
  const auto c0 = bf_extract<3>(a0, b0);
  return (c3 << 24) | (c2 << 16) | (c1 << 8) | c0;
}

inline uint32_t ebfu32(const uint32_t a, const uint32_t b) {
  return bf_extract_u<5>(a, b);
}

inline uint32_t ebfu16x2(const uint32_t a, const uint32_t b) {
  const auto a1 = static_cast<uint16_t>(a >> 16);
  const auto a0 = static_cast<uint16_t>(a);
  const auto b1 = b >> 16;
  const auto b0 = b & 0xffff;
  const auto c1 = bf_extract_u<4>(a1, b1);
  const auto c0 = bf_extract_u<4>(a0, b0);
  return (c1 << 16) | c0;
}

inline uint32_t ebfu8x4(const uint32_t a, const uint32_t b) {
  const auto a3 = static_cast<uint8_t>(a >> 24);
  const auto a2 = static_cast<uint8_t>(a >> 16);
  const auto a1 = static_cast<uint8_t>(a >> 8);
  const auto a0 = static_cast<uint8_t>(a);
  const auto b3 = b >> 24;
  const auto b2 = (b >> 16) & 0xff;
  const auto b1 = (b >> 8) & 0xff;
  const auto b0 = b & 0xff;
  const auto c3 = bf_extract_u<3>(a3, b3);
  const auto c2 = bf_extract_u<3>(a2, b2);
  const auto c1 = bf_extract_u<3>(a1, b1);
  const auto c0 = bf_extract_u<3>(a0, b0);
  return (c3 << 24) | (c2 << 16) | (c1 << 8) | c0;
}

inline uint32_t mkbf32(const uint32_t a, const uint32_t b) {
  return bf_make<5>(a, b);
}

inline uint32_t mkbf16x2(const uint32_t a, const uint32_t b) {
  const auto a1 = static_cast<uint16_t>(a >> 16);
  const auto a0 = static_cast<uint16_t>(a);
  const auto b1 = b >> 16;
  const auto b0 = b & 0xffff;
  const auto c1 = bf_make<4>(a1, b1);
  const auto c0 = bf_make<4>(a0, b0);
  return (c1 << 16) | c0;
}

inline uint32_t mkbf8x4(const uint32_t a, const uint32_t b) {
  const auto a3 = static_cast<uint8_t>(a >> 24);
  const auto a2 = static_cast<uint8_t>(a >> 16);
  const auto a1 = static_cast<uint8_t>(a >> 8);
  const auto a0 = static_cast<uint8_t>(a);
  const auto b3 = b >> 24;
  const auto b2 = (b >> 16) & 0xff;
  const auto b1 = (b >> 8) & 0xff;
  const auto b0 = b & 0xff;
  const auto c3 = bf_make<3>(a3, b3);
  const auto c2 = bf_make<3>(a2, b2);
  const auto c1 = bf_make<3>(a1, b1);
  const auto c0 = bf_make<3>(a0, b0);
  return (c3 << 24) | (c2 << 16) | (c1 << 8) | c0;
}

inline uint32_t ibf32(const uint32_t a, const uint32_t b, const uint32_t c) {
  return mkbf32(a, b) | (c & ~mkbf32(0xffffffff, b));
}

inline uint32_t ibf16x2(const uint32_t a, const uint32_t b, const uint32_t c) {
  return mkbf16x2(a, b) | (c & ~mkbf16x2(0xffffffff, b));
}

inline uint32_t ibf8x4(const uint32_t a, const uint32_t b, const uint32_t c) {
  return mkbf8x4(a, b) | (c & ~mkbf8x4(0xffffffff, b));
}

inline uint32_t crc32c_8(uint32_t crc, const uint32_t data) {
  static const uint32_t CRC32C_TAB[] = {0x00000000U,
                                        0x105ec76fU,
                                        0x20bd8edeU,
                                        0x30e349b1U,
                                        0x417b1dbcU,
                                        0x5125dad3U,
                                        0x61c69362U,
                                        0x7198540dU,
                                        0x82f63b78U,
                                        0x92a8fc17U,
                                        0xa24bb5a6U,
                                        0xb21572c9U,
                                        0xc38d26c4U,
                                        0xd3d3e1abU,
                                        0xe330a81aU,
                                        0xf36e6f75U};

  crc = CRC32C_TAB[(crc ^ data) & 0x0fu] ^ (crc >> 4);
  crc = CRC32C_TAB[(crc ^ (data >> 4)) & 0x0fu] ^ (crc >> 4);
  return crc;
}

uint32_t crc32c_16(uint32_t crc, const uint32_t data) {
  crc = crc32c_8(crc, data);
  return crc32c_8(crc, data >> 8);
}

uint32_t crc32c_32(uint32_t crc, const uint32_t data) {
  crc = crc32c_8(crc, data);
  crc = crc32c_8(crc, data >> 8);
  crc = crc32c_8(crc, data >> 16);
  return crc32c_8(crc, data >> 24);
}

inline uint32_t crc32_8(uint32_t crc, const uint32_t data) {
  static const uint32_t CRC32_TAB[] = {0x00000000U,
                                       0x1db71064U,
                                       0x3b6e20c8U,
                                       0x26d930acU,
                                       0x76dc4190U,
                                       0x6b6b51f4U,
                                       0x4db26158U,
                                       0x5005713cU,
                                       0xedb88320U,
                                       0xf00f9344U,
                                       0xd6d6a3e8U,
                                       0xcb61b38cU,
                                       0x9b64c2b0U,
                                       0x86d3d2d4U,
                                       0xa00ae278U,
                                       0xbdbdf21cU};

  crc = CRC32_TAB[(crc ^ data) & 0x0fu] ^ (crc >> 4);
  crc = CRC32_TAB[(crc ^ (data >> 4)) & 0x0fu] ^ (crc >> 4);
  return crc;
}

uint32_t crc32_16(uint32_t crc, const uint32_t data) {
  crc = crc32_8(crc, data);
  return crc32_8(crc, data >> 8);
}

uint32_t crc32_32(uint32_t crc, const uint32_t data) {
  crc = crc32_8(crc, data);
  crc = crc32_8(crc, data >> 8);
  crc = crc32_8(crc, data >> 16);
  return crc32_8(crc, data >> 24);
}

inline uint32_t saturate32(const int64_t x) {
  return (x > INT64_C(0x000000007fffffff))
             ? 0x7fffffffu
             : ((x < INT64_C(-0x0000000080000000)) ? 0x80000000u : static_cast<uint32_t>(x));
}

inline uint32_t saturate16(const int32_t x) {
  return (x > 0x00007fff)
             ? 0x7fffu
             : ((x < -0x00008000) ? 0x8000u : (static_cast<uint32_t>(x) & 0x0000ffffu));
}

inline uint32_t saturate8(const int16_t x) {
  return (x > 0x007f) ? 0x7fu : ((x < -0x0080) ? 0x80u : (static_cast<uint32_t>(x) & 0x00ffu));
}

inline uint32_t saturate4(const int8_t x) {
  return (x > 0x07) ? 0x7u : ((x < -0x08) ? 0x8u : (static_cast<uint32_t>(x) & 0x0fu));
}

inline uint32_t saturateu32(const uint64_t x) {
  return (x > UINT64_C(0x8000000000000000))
             ? 0x00000000u
             : ((x > UINT64_C(0x00000000ffffffff)) ? 0xffffffffu : static_cast<uint32_t>(x));
}

inline uint32_t saturateu16(const uint32_t x) {
  return (x > 0x80000000u) ? 0x0000u : ((x > 0x0000ffffu) ? 0xffffu : static_cast<uint32_t>(x));
}

inline uint32_t saturateu8(const uint16_t x) {
  return (x > 0x8000u) ? 0x00u : ((x > 0x00ffu) ? 0xffu : static_cast<uint32_t>(x));
}

inline uint32_t saturateu16_no_uf(const uint32_t x) {
  return (x > 0x0000ffffu) ? 0xffffu : static_cast<uint32_t>(x);
}

inline uint32_t saturateu8_no_uf(const uint16_t x) {
  return (x > 0x00ffu) ? 0xffu : static_cast<uint32_t>(x);
}

inline uint32_t saturateu4_no_uf(const uint8_t x) {
  return (x > 0x0fu) ? 0xfu : static_cast<uint32_t>(x);
}

inline uint32_t saturating_op_32(const uint32_t a,
                                 const uint32_t b,
                                 int64_t (*op)(int64_t, int64_t)) {
  const auto a64 = static_cast<int64_t>(static_cast<int32_t>(a));
  const auto b64 = static_cast<int64_t>(static_cast<int32_t>(b));
  return saturate32(op(a64, b64));
}

inline uint32_t saturating_op_16x2(const uint32_t a,
                                   const uint32_t b,
                                   int32_t (*op)(int32_t, int32_t)) {
  const auto a1 = static_cast<int32_t>(static_cast<int16_t>(a >> 16));
  const auto a2 = static_cast<int32_t>(static_cast<int16_t>(a));
  const auto b1 = static_cast<int32_t>(static_cast<int16_t>(b >> 16));
  const auto b2 = static_cast<int32_t>(static_cast<int16_t>(b));
  const auto c1 = saturate16(op(a1, b1));
  const auto c2 = saturate16(op(a2, b2));
  return (c1 << 16) | c2;
}

inline uint32_t saturating_op_8x4(const uint32_t a,
                                  const uint32_t b,
                                  int16_t (*op)(int16_t, int16_t)) {
  const auto a1 = static_cast<int16_t>(static_cast<int8_t>(a >> 24));
  const auto a2 = static_cast<int16_t>(static_cast<int8_t>(a >> 16));
  const auto a3 = static_cast<int16_t>(static_cast<int8_t>(a >> 8));
  const auto a4 = static_cast<int16_t>(static_cast<int8_t>(a));
  const auto b1 = static_cast<int16_t>(static_cast<int8_t>(b >> 24));
  const auto b2 = static_cast<int16_t>(static_cast<int8_t>(b >> 16));
  const auto b3 = static_cast<int16_t>(static_cast<int8_t>(b >> 8));
  const auto b4 = static_cast<int16_t>(static_cast<int8_t>(b));
  const auto c1 = saturate8(op(a1, b1));
  const auto c2 = saturate8(op(a2, b2));
  const auto c3 = saturate8(op(a3, b3));
  const auto c4 = saturate8(op(a4, b4));
  return (c1 << 24) | (c2 << 16) | (c3 << 8) | c4;
}

inline uint32_t saturating_op_u32(const uint32_t a,
                                  const uint32_t b,
                                  uint64_t (*op)(uint64_t, uint64_t)) {
  return saturateu32(op(static_cast<uint64_t>(a), static_cast<uint64_t>(b)));
}

inline uint32_t saturating_op_u16x2(const uint32_t a,
                                    const uint32_t b,
                                    uint32_t (*op)(uint32_t, uint32_t)) {
  const auto a1 = static_cast<uint32_t>(static_cast<uint16_t>(a >> 16));
  const auto a2 = static_cast<uint32_t>(static_cast<uint16_t>(a));
  const auto b1 = static_cast<uint32_t>(static_cast<uint16_t>(b >> 16));
  const auto b2 = static_cast<uint32_t>(static_cast<uint16_t>(b));
  const auto c1 = saturateu16(op(a1, b1));
  const auto c2 = saturateu16(op(a2, b2));
  return (c1 << 16) | c2;
}

inline uint32_t saturating_op_u8x4(const uint32_t a,
                                   const uint32_t b,
                                   uint16_t (*op)(uint16_t, uint16_t)) {
  const auto a1 = static_cast<uint16_t>(static_cast<uint8_t>(a >> 24));
  const auto a2 = static_cast<uint16_t>(static_cast<uint8_t>(a >> 16));
  const auto a3 = static_cast<uint16_t>(static_cast<uint8_t>(a >> 8));
  const auto a4 = static_cast<uint16_t>(static_cast<uint8_t>(a));
  const auto b1 = static_cast<uint16_t>(static_cast<uint8_t>(b >> 24));
  const auto b2 = static_cast<uint16_t>(static_cast<uint8_t>(b >> 16));
  const auto b3 = static_cast<uint16_t>(static_cast<uint8_t>(b >> 8));
  const auto b4 = static_cast<uint16_t>(static_cast<uint8_t>(b));
  const auto c1 = saturateu8(op(a1, b1));
  const auto c2 = saturateu8(op(a2, b2));
  const auto c3 = saturateu8(op(a3, b3));
  const auto c4 = saturateu8(op(a4, b4));
  return (c1 << 24) | (c2 << 16) | (c3 << 8) | c4;
}

inline uint32_t halve32(const int64_t x) {
  return static_cast<uint32_t>(x >> 1);
}

inline uint32_t halve16(const int32_t x) {
  return static_cast<uint32_t>(static_cast<uint16_t>(x >> 1));
}

inline uint32_t halve8(const int16_t x) {
  return static_cast<uint32_t>(static_cast<uint8_t>(x >> 1));
}

inline uint32_t halveu32(const uint64_t x) {
  return static_cast<uint32_t>(x >> 1);
}

inline uint32_t halveu16(const uint32_t x) {
  return static_cast<uint32_t>(static_cast<uint16_t>(x >> 1));
}

inline uint32_t halveu8(const uint16_t x) {
  return static_cast<uint32_t>(static_cast<uint8_t>(x >> 1));
}

inline uint32_t halving_op_32(const uint32_t a, const uint32_t b, int64_t (*op)(int64_t, int64_t)) {
  const auto a64 = static_cast<int64_t>(static_cast<int32_t>(a));
  const auto b64 = static_cast<int64_t>(static_cast<int32_t>(b));
  return halve32(op(a64, b64));
}

inline uint32_t halving_op_16x2(const uint32_t a,
                                const uint32_t b,
                                int32_t (*op)(int32_t, int32_t)) {
  const auto a1 = static_cast<int32_t>(static_cast<int16_t>(a >> 16));
  const auto a2 = static_cast<int32_t>(static_cast<int16_t>(a));
  const auto b1 = static_cast<int32_t>(static_cast<int16_t>(b >> 16));
  const auto b2 = static_cast<int32_t>(static_cast<int16_t>(b));
  const auto c1 = halve16(op(a1, b1));
  const auto c2 = halve16(op(a2, b2));
  return (c1 << 16) | c2;
}

inline uint32_t halving_op_8x4(const uint32_t a,
                               const uint32_t b,
                               int16_t (*op)(int16_t, int16_t)) {
  const auto a1 = static_cast<int16_t>(static_cast<int8_t>(a >> 24));
  const auto a2 = static_cast<int16_t>(static_cast<int8_t>(a >> 16));
  const auto a3 = static_cast<int16_t>(static_cast<int8_t>(a >> 8));
  const auto a4 = static_cast<int16_t>(static_cast<int8_t>(a));
  const auto b1 = static_cast<int16_t>(static_cast<int8_t>(b >> 24));
  const auto b2 = static_cast<int16_t>(static_cast<int8_t>(b >> 16));
  const auto b3 = static_cast<int16_t>(static_cast<int8_t>(b >> 8));
  const auto b4 = static_cast<int16_t>(static_cast<int8_t>(b));
  const auto c1 = halve8(op(a1, b1));
  const auto c2 = halve8(op(a2, b2));
  const auto c3 = halve8(op(a3, b3));
  const auto c4 = halve8(op(a4, b4));
  return (c1 << 24) | (c2 << 16) | (c3 << 8) | c4;
}

inline uint32_t halving_op_u32(const uint32_t a,
                               const uint32_t b,
                               uint64_t (*op)(uint64_t, uint64_t)) {
  return halveu32(op(static_cast<uint64_t>(a), static_cast<uint64_t>(b)));
}

inline uint32_t halving_op_u16x2(const uint32_t a,
                                 const uint32_t b,
                                 uint32_t (*op)(uint32_t, uint32_t)) {
  const auto a1 = static_cast<uint32_t>(static_cast<uint16_t>(a >> 16));
  const auto a2 = static_cast<uint32_t>(static_cast<uint16_t>(a));
  const auto b1 = static_cast<uint32_t>(static_cast<uint16_t>(b >> 16));
  const auto b2 = static_cast<uint32_t>(static_cast<uint16_t>(b));
  const auto c1 = halveu16(op(a1, b1));
  const auto c2 = halveu16(op(a2, b2));
  return (c1 << 16) | c2;
}

inline uint32_t halving_op_u8x4(const uint32_t a,
                                const uint32_t b,
                                uint16_t (*op)(uint16_t, uint16_t)) {
  const auto a1 = static_cast<uint16_t>(static_cast<uint8_t>(a >> 24));
  const auto a2 = static_cast<uint16_t>(static_cast<uint8_t>(a >> 16));
  const auto a3 = static_cast<uint16_t>(static_cast<uint8_t>(a >> 8));
  const auto a4 = static_cast<uint16_t>(static_cast<uint8_t>(a));
  const auto b1 = static_cast<uint16_t>(static_cast<uint8_t>(b >> 24));
  const auto b2 = static_cast<uint16_t>(static_cast<uint8_t>(b >> 16));
  const auto b3 = static_cast<uint16_t>(static_cast<uint8_t>(b >> 8));
  const auto b4 = static_cast<uint16_t>(static_cast<uint8_t>(b));
  const auto c1 = halveu8(op(a1, b1));
  const auto c2 = halveu8(op(a2, b2));
  const auto c3 = halveu8(op(a3, b3));
  const auto c4 = halveu8(op(a4, b4));
  return (c1 << 24) | (c2 << 16) | (c3 << 8) | c4;
}

inline uint32_t mul32(const uint32_t a, const uint32_t b) {
  return a * b;
}

inline uint32_t mul16x2(const uint32_t a, const uint32_t b) {
  const auto h1 = (a >> 16) * (b >> 16) << 16;
  const auto h0 = (a * b) & 0x0000ffffu;
  return h1 | h0;
}

inline uint32_t mul8x4(const uint32_t a, const uint32_t b) {
  const auto b3 = (a >> 24) * (b >> 24) << 24;
  const auto b2 = (((a >> 16) * (b >> 16)) & 0x000000ffu) << 16;
  const auto b1 = (((a >> 8) * (b >> 8)) & 0x000000ffu) << 8;
  const auto b0 = (a * b) & 0x000000ffu;
  return b3 | b2 | b1 | b0;
}

inline uint32_t mulhi32(const uint32_t a, const uint32_t b) {
  const int64_t p =
      static_cast<int64_t>(static_cast<int32_t>(a)) * static_cast<int64_t>(static_cast<int32_t>(b));
  return static_cast<uint32_t>(p >> 32u);
}

inline uint32_t mulhi16x2(const uint32_t a, const uint32_t b) {
  const auto a1 = static_cast<int32_t>(static_cast<int16_t>(a >> 16u));
  const auto a0 = static_cast<int32_t>(static_cast<int16_t>(a));
  const auto b1 = static_cast<int32_t>(static_cast<int16_t>(b >> 16u));
  const auto b0 = static_cast<int32_t>(static_cast<int16_t>(b));
  const auto c1 = static_cast<uint32_t>(a1 * b1) & 0xffff0000u;
  const auto c0 = static_cast<uint32_t>(a0 * b0) >> 16u;
  return c1 | c0;
}

inline uint32_t mulhi8x4(const uint32_t a, const uint32_t b) {
  const auto a3 = static_cast<int32_t>(static_cast<int8_t>(a >> 24u));
  const auto a2 = static_cast<int32_t>(static_cast<int8_t>(a >> 16u));
  const auto a1 = static_cast<int32_t>(static_cast<int8_t>(a >> 8u));
  const auto a0 = static_cast<int32_t>(static_cast<int8_t>(a));
  const auto b3 = static_cast<int32_t>(static_cast<int8_t>(b >> 24u));
  const auto b2 = static_cast<int32_t>(static_cast<int8_t>(b >> 16u));
  const auto b1 = static_cast<int32_t>(static_cast<int8_t>(b >> 8u));
  const auto b0 = static_cast<int32_t>(static_cast<int8_t>(b));
  const auto c3 = (static_cast<uint32_t>(a3 * b3) & 0x0000ff00u) << 16u;
  const auto c2 = (static_cast<uint32_t>(a2 * b2) & 0x0000ff00u) << 8u;
  const auto c1 = (static_cast<uint32_t>(a1 * b1) & 0x0000ff00u);
  const auto c0 = (static_cast<uint32_t>(a0 * b0) & 0x0000ff00u) >> 8u;
  return c3 | c2 | c1 | c0;
}

inline uint32_t mulhiu32(const uint32_t a, const uint32_t b) {
  const uint64_t p = static_cast<uint64_t>(a) * static_cast<uint64_t>(b);
  return static_cast<uint32_t>(p >> 32u);
}

inline uint32_t mulhiu16x2(const uint32_t a, const uint32_t b) {
  const auto h1 = (a >> 16) * (b >> 16) & 0xffff0000u;
  const auto h0 = ((a & 0x0000ffffu) * (b & 0x0000ffffu)) >> 16;
  return h1 | h0;
}

inline uint32_t mulhiu8x4(const uint32_t a, const uint32_t b) {
  const auto b3 = ((a & 0xff000000u) >> 16u) * ((b & 0xff000000u) >> 16u) & 0xff000000u;
  const auto b2 = (((a & 0x00ff0000u) >> 12u) * ((b & 0x00ff0000u) >> 12u)) & 0x00ff0000u;
  const auto b1 = ((a & 0x0000ff00u) >> 8u) * ((b & 0x0000ff00u) >> 8u) & 0x0000ff00u;
  const auto b0 = ((a & 0x000000ffu) * (b & 0x000000ffu)) >> 8u;
  return b3 | b2 | b1 | b0;
}

inline uint32_t madd32(const uint32_t a, const uint32_t b, const uint32_t c) {
  return c + a * b;
}

inline uint32_t madd16x2(const uint32_t a, const uint32_t b, const uint32_t c) {
  const auto h1 = ((c >> 16) + (a >> 16) * (b >> 16)) << 16;
  const auto h0 = (c + a * b) & 0x0000ffffu;
  return h1 | h0;
}

inline uint32_t madd8x4(const uint32_t a, const uint32_t b, const uint32_t c) {
  const auto b3 = ((c >> 24) + (a >> 24) * (b >> 24)) << 24;
  const auto b2 = (((c >> 16) + (a >> 16) * (b >> 16)) & 0x000000ffu) << 16;
  const auto b1 = (((c >> 8) + (a >> 8) * (b >> 8)) & 0x000000ffu) << 8;
  const auto b0 = (c + a * b) & 0x000000ffu;
  return b3 | b2 | b1 | b0;
}

template <typename T>
inline T div_allow_zero(const T a, const T b) {
  return b != static_cast<T>(0) ? (a / b) : static_cast<T>(-1);
}

template <typename T>
inline T mod_allow_zero(const T a, const T b) {
  return b != static_cast<T>(0) ? (a % b) : a;
}

inline uint32_t div32(const uint32_t a, const uint32_t b) {
  return static_cast<uint32_t>(div_allow_zero(static_cast<int32_t>(a), static_cast<int32_t>(b)));
}

inline uint32_t div16x2(const uint32_t a, const uint32_t b) {
  const auto a1 = static_cast<int32_t>(static_cast<int16_t>(a >> 16u));
  const auto a0 = static_cast<int32_t>(static_cast<int16_t>(a));
  const auto b1 = static_cast<int32_t>(static_cast<int16_t>(b >> 16u));
  const auto b0 = static_cast<int32_t>(static_cast<int16_t>(b));
  const auto c1 = (static_cast<uint32_t>(div_allow_zero(a1, b1)) & 0x0000ffffu) << 16u;
  const auto c0 = static_cast<uint32_t>(div_allow_zero(a0, b0)) & 0x0000ffffu;
  return c1 | c0;
}

inline uint32_t div8x4(const uint32_t a, const uint32_t b) {
  const auto a3 = static_cast<int32_t>(static_cast<int8_t>(a >> 24u));
  const auto a2 = static_cast<int32_t>(static_cast<int8_t>(a >> 16u));
  const auto a1 = static_cast<int32_t>(static_cast<int8_t>(a >> 8u));
  const auto a0 = static_cast<int32_t>(static_cast<int8_t>(a));
  const auto b3 = static_cast<int32_t>(static_cast<int8_t>(b >> 24u));
  const auto b2 = static_cast<int32_t>(static_cast<int8_t>(b >> 16u));
  const auto b1 = static_cast<int32_t>(static_cast<int8_t>(b >> 8u));
  const auto b0 = static_cast<int32_t>(static_cast<int8_t>(b));
  const auto c3 = (static_cast<uint32_t>(div_allow_zero(a3, b3)) & 0x000000ffu) << 24u;
  const auto c2 = (static_cast<uint32_t>(div_allow_zero(a2, b2)) & 0x000000ffu) << 16u;
  const auto c1 = (static_cast<uint32_t>(div_allow_zero(a1, b1)) & 0x000000ffu) << 8u;
  const auto c0 = static_cast<uint32_t>(div_allow_zero(a0, b0)) & 0x000000ffu;
  return c3 | c2 | c1 | c0;
}

inline uint32_t divu32(const uint32_t a, const uint32_t b) {
  return div_allow_zero(a, b);
}

inline uint32_t divu16x2(const uint32_t a, const uint32_t b) {
  const auto a1 = a >> 16u;
  const auto a0 = a & 0x0000ffff;
  const auto b1 = b >> 16u;
  const auto b0 = b & 0x0000ffff;
  const auto c1 = div_allow_zero(a1, b1) << 16u;
  const auto c0 = div_allow_zero(a0, b0);
  return c1 | c0;
}

inline uint32_t divu8x4(const uint32_t a, const uint32_t b) {
  const auto a3 = a >> 24u;
  const auto a2 = (a >> 16u) & 0x000000ff;
  const auto a1 = (a >> 8u) & 0x000000ff;
  const auto a0 = a & 0x000000ff;
  const auto b3 = b >> 24u;
  const auto b2 = (b >> 16u) & 0x000000ff;
  const auto b1 = (b >> 8u) & 0x000000ff;
  const auto b0 = b & 0x000000ff;
  const auto c3 = div_allow_zero(a3, b3) << 24u;
  const auto c2 = div_allow_zero(a2, b2) << 16u;
  const auto c1 = div_allow_zero(a1, b1) << 8u;
  const auto c0 = div_allow_zero(a0, b0);
  return c3 | c2 | c1 | c0;
}

inline uint32_t rem32(const uint32_t a, const uint32_t b) {
  return static_cast<uint32_t>(mod_allow_zero(static_cast<int32_t>(a), static_cast<int32_t>(b)));
}

inline uint32_t rem16x2(const uint32_t a, const uint32_t b) {
  const auto a1 = static_cast<int32_t>(static_cast<int16_t>(a >> 16u));
  const auto a0 = static_cast<int32_t>(static_cast<int16_t>(a));
  const auto b1 = static_cast<int32_t>(static_cast<int16_t>(b >> 16u));
  const auto b0 = static_cast<int32_t>(static_cast<int16_t>(b));
  const auto c1 = (static_cast<uint32_t>(mod_allow_zero(a1, b1)) & 0x0000ffffu) << 16u;
  const auto c0 = static_cast<uint32_t>(mod_allow_zero(a0, b0)) & 0x0000ffffu;
  return c1 | c0;
}

inline uint32_t rem8x4(const uint32_t a, const uint32_t b) {
  const auto a3 = static_cast<int32_t>(static_cast<int8_t>(a >> 24u));
  const auto a2 = static_cast<int32_t>(static_cast<int8_t>(a >> 16u));
  const auto a1 = static_cast<int32_t>(static_cast<int8_t>(a >> 8u));
  const auto a0 = static_cast<int32_t>(static_cast<int8_t>(a));
  const auto b3 = static_cast<int32_t>(static_cast<int8_t>(b >> 24u));
  const auto b2 = static_cast<int32_t>(static_cast<int8_t>(b >> 16u));
  const auto b1 = static_cast<int32_t>(static_cast<int8_t>(b >> 8u));
  const auto b0 = static_cast<int32_t>(static_cast<int8_t>(b));
  const auto c3 = (static_cast<uint32_t>(mod_allow_zero(a3, b3)) & 0x000000ffu) << 24u;
  const auto c2 = (static_cast<uint32_t>(mod_allow_zero(a2, b2)) & 0x000000ffu) << 16u;
  const auto c1 = (static_cast<uint32_t>(mod_allow_zero(a1, b1)) & 0x000000ffu) << 8u;
  const auto c0 = static_cast<uint32_t>(mod_allow_zero(a0, b0)) & 0x000000ffu;
  return c3 | c2 | c1 | c0;
}

inline uint32_t remu32(const uint32_t a, const uint32_t b) {
  return mod_allow_zero(a, b);
}

inline uint32_t remu16x2(const uint32_t a, const uint32_t b) {
  const auto a1 = a >> 16u;
  const auto a0 = a & 0x0000ffff;
  const auto b1 = b >> 16u;
  const auto b0 = b & 0x0000ffff;
  const auto c1 = mod_allow_zero(a1, b1) << 16u;
  const auto c0 = mod_allow_zero(a0, b0);
  return c1 | c0;
}

inline uint32_t remu8x4(const uint32_t a, const uint32_t b) {
  const auto a3 = a >> 24u;
  const auto a2 = (a >> 16u) & 0x000000ff;
  const auto a1 = (a >> 8u) & 0x000000ff;
  const auto a0 = a & 0x000000ff;
  const auto b3 = b >> 24u;
  const auto b2 = (b >> 16u) & 0x000000ff;
  const auto b1 = (b >> 8u) & 0x000000ff;
  const auto b0 = b & 0x000000ff;
  const auto c3 = mod_allow_zero(a3, b3) << 24u;
  const auto c2 = mod_allow_zero(a2, b2) << 16u;
  const auto c1 = mod_allow_zero(a1, b1) << 8u;
  const auto c0 = mod_allow_zero(a0, b0);
  return c3 | c2 | c1 | c0;
}

inline uint32_t fpack32(const uint32_t a, const uint32_t b) {
  return f16x2_t::from_f32x2(a, b).packf();
}

inline uint32_t fpack16x2(const uint32_t a, const uint32_t b) {
  return f8x4_t::from_f16x4(f16x2_t(a), f16x2_t(b)).packf();
}

inline uint32_t fadd32(const uint32_t a, const uint32_t b) {
  return as_u32(as_f32(a) + as_f32(b));
}

inline uint32_t fadd16x2(const uint32_t a, const uint32_t b) {
  return (f16x2_t(a) + f16x2_t(b)).packf();
}

inline uint32_t fadd8x4(const uint32_t a, const uint32_t b) {
  return (f8x4_t(a) + f8x4_t(b)).packf();
}

inline uint32_t fsub32(const uint32_t a, const uint32_t b) {
  return as_u32(as_f32(a) - as_f32(b));
}

inline uint32_t fsub16x2(const uint32_t a, const uint32_t b) {
  return (f16x2_t(a) - f16x2_t(b)).packf();
}

inline uint32_t fsub8x4(const uint32_t a, const uint32_t b) {
  return (f8x4_t(a) - f8x4_t(b)).packf();
}

inline uint32_t fmul32(const uint32_t a, const uint32_t b) {
  return as_u32(as_f32(a) * as_f32(b));
}

inline uint32_t fmul16x2(const uint32_t a, const uint32_t b) {
  return (f16x2_t(a) * f16x2_t(b)).packf();
}

inline uint32_t fmul8x4(const uint32_t a, const uint32_t b) {
  return (f8x4_t(a) * f8x4_t(b)).packf();
}

inline uint32_t fdiv32(const uint32_t a, const uint32_t b) {
  return as_u32(as_f32(a) / as_f32(b));
}

inline uint32_t fdiv16x2(const uint32_t a, const uint32_t b) {
  return (f16x2_t(a) / f16x2_t(b)).packf();
}

inline uint32_t fdiv8x4(const uint32_t a, const uint32_t b) {
  return (f8x4_t(a) / f8x4_t(b)).packf();
}

inline uint32_t fsqrt32(const uint32_t a, const uint32_t b) {
  (void)b;
  return as_u32(std::sqrt(as_f32(a)));
}

inline uint32_t fsqrt16x2(const uint32_t a, const uint32_t b) {
  (void)b;
  return f16x2_t(a).sqrt().packf();
}

inline uint32_t fsqrt8x4(const uint32_t a, const uint32_t b) {
  (void)b;
  return f8x4_t(a).sqrt().packf();
}

inline uint32_t fmin32(const uint32_t a, const uint32_t b) {
  return as_u32(std::min(as_f32(a), as_f32(b)));
}

inline uint32_t fmin16x2(const uint32_t a, const uint32_t b) {
  return min(f16x2_t(a), f16x2_t(b)).packf();
}

inline uint32_t fmin8x4(const uint32_t a, const uint32_t b) {
  return min(f8x4_t(a), f8x4_t(b)).packf();
}

inline uint32_t fmax32(const uint32_t a, const uint32_t b) {
  return as_u32(std::max(as_f32(a), as_f32(b)));
}

inline uint32_t fmax16x2(const uint32_t a, const uint32_t b) {
  return max(f16x2_t(a), f16x2_t(b)).packf();
}

inline uint32_t fmax8x4(const uint32_t a, const uint32_t b) {
  return max(f8x4_t(a), f8x4_t(b)).packf();
}

inline uint32_t clz32(const uint32_t x) {
#if defined(__GNUC__) || defined(__clang__)
  return (x == 0u) ? 32u : static_cast<uint32_t>(__builtin_clz(x));
#else
  uint32_t count = 0u;
  for (; (count != 32u) && ((x & (0x80000000u >> count)) == 0u); ++count)
    ;
  return count;
#endif
}

inline uint32_t clz16x2(const uint32_t x) {
  return (clz32(x | 0x00008000u) << 16u) | (clz32((x << 16u) | 0x00008000u));
}

inline uint32_t clz8x4(const uint32_t x) {
  return (clz32(x | 0x00800000u) << 24u) | (clz32((x << 8u) | 0x00800000u) << 16u) |
         (clz32((x << 16u) | 0x00800000u) << 8u) | (clz32((x << 24u) | 0x00800000u));
}

inline uint32_t popcnt32(const uint32_t x) {
#if defined(__GNUC__) || defined(__clang__)
  return static_cast<uint32_t>(__builtin_popcount(x));
#else
  uint32_t count = 0u;
  for (int i = 0; i < 32; ++i) {
    if ((x & (0x80000000u >> i)) == 1u) {
      ++count;
    }
  }
  return count;
#endif
}

inline uint32_t popcnt16x2(const uint32_t x) {
  return (popcnt32(x & 0xffff0000u) << 16u) | popcnt32(x & 0x0000ffffu);
}

inline uint32_t popcnt8x4(const uint32_t x) {
  return (popcnt32(x & 0xff000000u) << 24u) | (popcnt32(x & 0x00ff0000u) << 16u) |
         (popcnt32(x & 0x0000ff00u) << 8u) | popcnt32(x & 0x000000ffu);
}

inline uint32_t rev32(const uint32_t x) {
  return ((x >> 31u) & 0x00000001u) | ((x >> 29u) & 0x00000002u) | ((x >> 27u) & 0x00000004u) |
         ((x >> 25u) & 0x00000008u) | ((x >> 23u) & 0x00000010u) | ((x >> 21u) & 0x00000020u) |
         ((x >> 19u) & 0x00000040u) | ((x >> 17u) & 0x00000080u) | ((x >> 15u) & 0x00000100u) |
         ((x >> 13u) & 0x00000200u) | ((x >> 11u) & 0x00000400u) | ((x >> 9u) & 0x00000800u) |
         ((x >> 7u) & 0x00001000u) | ((x >> 5u) & 0x00002000u) | ((x >> 3u) & 0x00004000u) |
         ((x >> 1u) & 0x00008000u) | ((x << 1u) & 0x00010000u) | ((x << 3u) & 0x00020000u) |
         ((x << 5u) & 0x00040000u) | ((x << 7u) & 0x00080000u) | ((x << 9u) & 0x00100000u) |
         ((x << 11u) & 0x00200000u) | ((x << 13u) & 0x00400000u) | ((x << 15u) & 0x00800000u) |
         ((x << 17u) & 0x01000000u) | ((x << 19u) & 0x02000000u) | ((x << 21u) & 0x04000000u) |
         ((x << 23u) & 0x08000000u) | ((x << 25u) & 0x10000000u) | ((x << 27u) & 0x20000000u) |
         ((x << 29u) & 0x40000000u) | ((x << 31u) & 0x80000000u);
}

inline uint32_t rev16x2(const uint32_t x) {
  return ((x >> 15u) & 0x00010001u) | ((x >> 13u) & 0x00020002u) | ((x >> 11u) & 0x00040004u) |
         ((x >> 9u) & 0x00080008u) | ((x >> 7u) & 0x00100010u) | ((x >> 5u) & 0x00200020u) |
         ((x >> 3u) & 0x00400040u) | ((x >> 1u) & 0x00800080u) | ((x << 1u) & 0x01000100u) |
         ((x << 3u) & 0x02000200u) | ((x << 5u) & 0x04000400u) | ((x << 7u) & 0x08000800u) |
         ((x << 9u) & 0x10001000u) | ((x << 11u) & 0x20002000u) | ((x << 13u) & 0x40004000u) |
         ((x << 15u) & 0x80008000u);
}

inline uint32_t rev8x4(const uint32_t x) {
  return ((x >> 7u) & 0x01010101u) | ((x >> 5u) & 0x02020202u) | ((x >> 3u) & 0x04040404u) |
         ((x >> 1u) & 0x08080808u) | ((x << 1u) & 0x10101010u) | ((x << 3u) & 0x20202020u) |
         ((x << 5u) & 0x40404040u) | ((x << 7u) & 0x80808080u);
}

inline uint8_t shuf_op(const uint8_t x, const bool fill, const bool sign_fill) {
  const uint8_t fill_bits = (sign_fill && ((x & 0x80u) != 0u)) ? 0xffu : 0x00u;
  return fill ? fill_bits : x;
}

inline uint32_t shuf32(const uint32_t x, const uint32_t idx) {
  // Extract the four bytes from x.
  uint8_t xv[4];
  xv[0] = static_cast<uint8_t>(x);
  xv[1] = static_cast<uint8_t>(x >> 8u);
  xv[2] = static_cast<uint8_t>(x >> 16u);
  xv[3] = static_cast<uint8_t>(x >> 24u);

  // Extract the four indices from idx.
  uint8_t idxv[4];
  idxv[0] = static_cast<uint8_t>(idx & 3u);
  idxv[1] = static_cast<uint8_t>((idx >> 3u) & 3u);
  idxv[2] = static_cast<uint8_t>((idx >> 6u) & 3u);
  idxv[3] = static_cast<uint8_t>((idx >> 9u) & 3u);

  // Extract the four fill operation descriptions from idx.
  bool fillv[4];
  fillv[0] = ((idx & 4u) != 0u);
  fillv[1] = ((idx & (4u << 3u)) != 0u);
  fillv[2] = ((idx & (4u << 6u)) != 0u);
  fillv[3] = ((idx & (4u << 9u)) != 0u);

  // Sign-fill or zero-fill?
  const bool sign_fill = (((idx >> 12u) & 1u) != 0u);

  // Combine the parts into four new bytes.
  uint8_t yv[4];
  yv[0] = shuf_op(xv[idxv[0]], fillv[0], sign_fill);
  yv[1] = shuf_op(xv[idxv[1]], fillv[1], sign_fill);
  yv[2] = shuf_op(xv[idxv[2]], fillv[2], sign_fill);
  yv[3] = shuf_op(xv[idxv[3]], fillv[3], sign_fill);

  // Combine the four bytes into a 32-bit word.
  return static_cast<uint32_t>(yv[0]) | (static_cast<uint32_t>(yv[1]) << 8u) |
         (static_cast<uint32_t>(yv[2]) << 16u) | (static_cast<uint32_t>(yv[3]) << 24u);
}

inline uint32_t pack32(const uint32_t a, const uint32_t b) {
  return ((a & 0x0000ffffu) << 16) | (b & 0x0000ffffu);
}

inline uint32_t pack16x2(const uint32_t a, const uint32_t b) {
  return ((a & 0x00ff00ffu) << 8u) | (b & 0x00ff00ffu);
}

inline uint32_t pack8x4(const uint32_t a, const uint32_t b) {
  return ((a & 0x0f0f0f0fu) << 4u) | (b & 0x0f0f0f0fu);
}

inline uint32_t packs32(const uint32_t a, const uint32_t b) {
  return pack32(saturate16(static_cast<int32_t>(a)), saturate16(static_cast<int32_t>(b)));
}

inline uint32_t packs16x2(const uint32_t a, const uint32_t b) {
  const auto a1 = saturate8(static_cast<int16_t>(a >> 16));
  const auto a0 = saturate8(static_cast<int16_t>(a));
  const auto b1 = saturate8(static_cast<int16_t>(b >> 16));
  const auto b0 = saturate8(static_cast<int16_t>(b));
  return (a1 << 24) | (a0 << 8) | (b1 << 16) | b0;
}

inline uint32_t packs8x4(const uint32_t a, const uint32_t b) {
  const auto a3 = saturate4(static_cast<int8_t>(a >> 24));
  const auto a2 = saturate4(static_cast<int8_t>(a >> 16));
  const auto a1 = saturate4(static_cast<int8_t>(a >> 8));
  const auto a0 = saturate4(static_cast<int8_t>(a));
  const auto b3 = saturate4(static_cast<int8_t>(b >> 24));
  const auto b2 = saturate4(static_cast<int8_t>(b >> 16));
  const auto b1 = saturate4(static_cast<int8_t>(b >> 8));
  const auto b0 = saturate4(static_cast<int8_t>(b));
  return (a3 << 28) | (a2 << 20) | (a1 << 12) | (a0 << 4) | (b3 << 24) | (b2 << 16) | (b1 << 8) |
         b0;
}

inline uint32_t packsu32(const uint32_t a, const uint32_t b) {
  return pack32(saturateu16_no_uf(a), saturateu16_no_uf(b));
}

inline uint32_t packsu16x2(const uint32_t a, const uint32_t b) {
  const auto a1 = saturateu8_no_uf(static_cast<uint16_t>(a >> 16));
  const auto a0 = saturateu8_no_uf(static_cast<uint16_t>(a));
  const auto b1 = saturateu8_no_uf(static_cast<uint16_t>(b >> 16));
  const auto b0 = saturateu8_no_uf(static_cast<uint16_t>(b));
  return (a1 << 24) | (a0 << 8) | (b1 << 16) | b0;
}

inline uint32_t packsu8x4(const uint32_t a, const uint32_t b) {
  const auto a3 = saturateu4_no_uf(static_cast<uint8_t>(a >> 24));
  const auto a2 = saturateu4_no_uf(static_cast<uint8_t>(a >> 16));
  const auto a1 = saturateu4_no_uf(static_cast<uint8_t>(a >> 8));
  const auto a0 = saturateu4_no_uf(static_cast<uint8_t>(a));
  const auto b3 = saturateu4_no_uf(static_cast<uint8_t>(b >> 24));
  const auto b2 = saturateu4_no_uf(static_cast<uint8_t>(b >> 16));
  const auto b1 = saturateu4_no_uf(static_cast<uint8_t>(b >> 8));
  const auto b0 = saturateu4_no_uf(static_cast<uint8_t>(b));
  return (a3 << 28) | (a2 << 20) | (a1 << 12) | (a0 << 4) | (b3 << 24) | (b2 << 16) | (b1 << 8) |
         b0;
}

inline uint32_t packhi32(const uint32_t a, const uint32_t b) {
  return (a & 0xffff0000u) | (b >> 16);
}

inline uint32_t packhi16x2(const uint32_t a, const uint32_t b) {
  return (a & 0xff00ff00u) | ((b & 0xff00ff00u) >> 8);
}

inline uint32_t packhi8x4(const uint32_t a, const uint32_t b) {
  return (a & 0xf0f0f0f0u) | ((b & 0xf0f0f0f0u) >> 4);
}

inline uint32_t roundhi32to16(const uint32_t x) {
  const auto y = static_cast<int64_t>(static_cast<int32_t>(x)) + (1 << 15);
  return y > 0x7fffffff ? 0x7fff : ((y >> 16) & 0xffff);
}

inline uint32_t roundhi16to8(const uint16_t x) {
  const auto y = static_cast<int32_t>(static_cast<int16_t>(x)) + (1 << 7);
  return y > 0x7fff ? 0x7f : ((y >> 8) & 0xff);
}

inline uint32_t roundhi8to4(const uint8_t x) {
  const auto y = static_cast<int32_t>(static_cast<int8_t>(x)) + (1 << 3);
  return y > 0x7f ? 0x7 : ((y >> 4) & 0xf);
}

inline uint32_t packhir32(const uint32_t a, const uint32_t b) {
  return (roundhi32to16(a) << 16) | roundhi32to16(b);
}

inline uint32_t packhir16x2(const uint32_t a, const uint32_t b) {
  const auto a1 = roundhi16to8(static_cast<uint16_t>(a >> 16));
  const auto a0 = roundhi16to8(static_cast<uint16_t>(a & 0xffffu));
  const auto b1 = roundhi16to8(static_cast<uint16_t>(b >> 16));
  const auto b0 = roundhi16to8(static_cast<uint16_t>(b & 0xffffu));
  return (a1 << 24) | (a0 << 8) | (b1 << 16) | b0;
}

inline uint32_t packhir8x4(const uint32_t a, const uint32_t b) {
  const auto a3 = roundhi8to4(static_cast<uint8_t>(a >> 24));
  const auto a2 = roundhi8to4(static_cast<uint8_t>((a >> 16) & 0xffu));
  const auto a1 = roundhi8to4(static_cast<uint8_t>((a >> 8) & 0xffu));
  const auto a0 = roundhi8to4(static_cast<uint8_t>(a & 0xffu));
  const auto b3 = roundhi8to4(static_cast<uint8_t>(b >> 24));
  const auto b2 = roundhi8to4(static_cast<uint8_t>((b >> 16) & 0xffu));
  const auto b1 = roundhi8to4(static_cast<uint8_t>((b >> 8) & 0xffu));
  const auto b0 = roundhi8to4(static_cast<uint8_t>(b & 0xffu));
  return (a3 << 28) | (a2 << 20) | (a1 << 12) | (a0 << 4) | (b3 << 24) | (b2 << 16) | (b1 << 8) |
         b0;
}

inline uint32_t roundhiu32to16(const uint32_t x) {
  const auto y = static_cast<uint64_t>(x) + (1 << 15);
  return y > 0xffffffffu ? 0xffff : (y >> 16);
}

inline uint32_t roundhiu16to8(const uint16_t x) {
  const auto y = static_cast<uint32_t>(x) + (1 << 7);
  return y > 0xffffu ? 0xff : (y >> 8);
}

inline uint32_t roundhiu8to4(const uint8_t x) {
  const auto y = static_cast<uint32_t>(x) + (1 << 3);
  return y > 0xffu ? 0xf : (y >> 4);
}

inline uint32_t packhiur32(const uint32_t a, const uint32_t b) {
  return (roundhiu32to16(a) << 16) | roundhiu32to16(b);
}

inline uint32_t packhiur16x2(const uint32_t a, const uint32_t b) {
  const auto a1 = roundhiu16to8(static_cast<uint16_t>(a >> 16));
  const auto a0 = roundhiu16to8(static_cast<uint16_t>(a & 0xffffu));
  const auto b1 = roundhiu16to8(static_cast<uint16_t>(b >> 16));
  const auto b0 = roundhiu16to8(static_cast<uint16_t>(b & 0xffffu));
  return (a1 << 24) | (a0 << 8) | (b1 << 16) | b0;
}

inline uint32_t packhiur8x4(const uint32_t a, const uint32_t b) {
  const auto a3 = roundhiu8to4(static_cast<uint8_t>(a >> 24));
  const auto a2 = roundhiu8to4(static_cast<uint8_t>((a >> 16) & 0xffu));
  const auto a1 = roundhiu8to4(static_cast<uint8_t>((a >> 8) & 0xffu));
  const auto a0 = roundhiu8to4(static_cast<uint8_t>(a & 0xffu));
  const auto b3 = roundhiu8to4(static_cast<uint8_t>(b >> 24));
  const auto b2 = roundhiu8to4(static_cast<uint8_t>((b >> 16) & 0xffu));
  const auto b1 = roundhiu8to4(static_cast<uint8_t>((b >> 8) & 0xffu));
  const auto b0 = roundhiu8to4(static_cast<uint8_t>(b & 0xffu));
  return (a3 << 28) | (a2 << 20) | (a1 << 12) | (a0 << 4) | (b3 << 24) | (b2 << 16) | (b1 << 8) |
         b0;
}

inline bool float32_isnan(const uint32_t x) {
  return ((x & 0x7F800000u) == 0x7F800000u) && ((x & 0x007fffffu) != 0u);
}

inline uint32_t itof32(const uint32_t a, const uint32_t b) {
  const float f = static_cast<float>(static_cast<int32_t>(a));
  return as_u32(std::ldexp(f, -static_cast<int32_t>(b)));
}

inline uint32_t itof16x2(const uint32_t a, const uint32_t b) {
  return f16x2_t::itof(a, b).packf();
}

inline uint32_t itof8x4(const uint32_t a, const uint32_t b) {
  return f8x4_t::itof(a, b).packf();
}

inline uint32_t utof32(const uint32_t a, const uint32_t b) {
  const float f = static_cast<float>(a);
  return as_u32(std::ldexp(f, -static_cast<int32_t>(b)));
}

inline uint32_t utof16x2(const uint32_t a, const uint32_t b) {
  return f16x2_t::utof(a, b).packf();
}

inline uint32_t utof8x4(const uint32_t a, const uint32_t b) {
  return f8x4_t::utof(a, b).packf();
}

inline uint32_t ftoi32(const uint32_t a, const uint32_t b) {
  const float f = std::ldexp(as_f32(a), static_cast<int32_t>(b));
  return static_cast<uint32_t>(static_cast<int32_t>(f));
}

inline uint32_t ftoi16x2(const uint32_t a, const uint32_t b) {
  return f16x2_t(a).packi(b);
}

inline uint32_t ftoi8x4(const uint32_t a, const uint32_t b) {
  return f8x4_t(a).packi(b);
}

inline uint32_t ftou32(const uint32_t a, const uint32_t b) {
  const float f = std::ldexp(as_f32(a), static_cast<int32_t>(b));
  return static_cast<uint32_t>(f);
}

inline uint32_t ftou16x2(const uint32_t a, const uint32_t b) {
  return f16x2_t(a).packu(b);
}

inline uint32_t ftou8x4(const uint32_t a, const uint32_t b) {
  return f8x4_t(a).packu(b);
}

inline uint32_t ftoir32(const uint32_t a, const uint32_t b) {
  const float f = std::ldexp(as_f32(a), static_cast<int32_t>(b));
  return static_cast<uint32_t>(static_cast<int32_t>(std::round(f)));
}

inline uint32_t ftoir16x2(const uint32_t a, const uint32_t b) {
  return f16x2_t(a).packir(b);
}

inline uint32_t ftoir8x4(const uint32_t a, const uint32_t b) {
  return f8x4_t(a).packir(b);
}

inline uint32_t ftour32(const uint32_t a, const uint32_t b) {
  const float f = std::ldexp(as_f32(a), static_cast<int32_t>(b));
  return static_cast<uint32_t>(std::round(f));
}

inline uint32_t ftour16x2(const uint32_t a, const uint32_t b) {
  return f16x2_t(a).packur(b);
}

inline uint32_t ftour8x4(const uint32_t a, const uint32_t b) {
  return f8x4_t(a).packur(b);
}
}  // namespace

cpu_simple_t::cpu_simple_t(ram_t& ram, perf_symbols_t& perf_symbols) : cpu_t(ram, perf_symbols) {
  const uint32_t MMIO_START = 0xc0000000u;
  const auto has_mc1_mmio_regs = m_ram.valid_range(MMIO_START, 64);
  m_mc1_mmio = has_mc1_mmio_regs ? reinterpret_cast<uint32_t*>(&m_ram.at(MMIO_START)) : nullptr;
}

uint32_t cpu_simple_t::xchgsr(uint32_t a, uint32_t b, bool a_is_z_reg) {
  // 1) Read system register.
  uint32_t result = 0u;
  switch (b) {
    case 0x00000000u:
      // CPU_FEATURES_0 (CPU feature flags register 0):
      //   VM (Vector operatoin module)                  = 1 << 0
      //   PM (Packed operation module)                  = 1 << 1
      //   FM (Floating-point module)                    = 1 << 2
      //   SM (Saturating and halving arithmetic module) = 1 << 3
      return 0x0000000fu;

    case 0x00000001u:
    case 0x00000002u:
    case 0x00000003u:
    case 0x00000004u:
    case 0x00000005u:
    case 0x00000006u:
    case 0x00000007u:
    case 0x00000008u:
    case 0x00000009u:
    case 0x0000000au:
    case 0x0000000bu:
    case 0x0000000cu:
    case 0x0000000du:
    case 0x0000000eu:
    case 0x0000000fu:
      // CPU_FEATURES_1-15 (CPU feature flags register 1-15):
      //   Reserved (zero)
      result = 0x00000000u;
      break;

    case 0x00000010u:
      // MAX_VL (Maximum vector length register).
      result = NUM_VECTOR_ELEMENTS;
      break;

    case 0x00000011u:
      // LOG2_MAX_VL (Maximum vector length register).
      result = LOG2_NUM_VECTOR_ELEMENTS;
      break;

    default:
      break;
  }

  // 2) Write system register (optional).
  if (!a_is_z_reg) {
    switch (b) {
        // TODO(m): There are currently no writable system registers.

      default:
        break;
    }
  }

  return result;
}

void cpu_simple_t::update_mc1_clkcnt() {
  if (m_mc1_mmio) {
    const uint32_t clkcntlo = static_cast<uint32_t>(m_total_cycle_count);
    const uint32_t clkcnthi = static_cast<uint32_t>(m_total_cycle_count >> 32);
    m_mc1_mmio[0] = clkcntlo;  // CLKCNTLO
    m_mc1_mmio[4] = clkcnthi;  // CLKCNTHI
  }
}

uint32_t cpu_simple_t::run(const uint32_t start_addr, const int64_t max_cycles) {
  begin_simulation();

  m_syscalls.clear();
  m_regs[REG_PC] = start_addr;
  m_fetched_instr_count = 0u;
  m_vector_loop_count = 0u;
  m_total_cycle_count = 0u;

  // Initialize the pipeline state.
  vector_state_t vector = vector_state_t();
  decode_t decode = decode_t();

  try {
    while (!m_syscalls.terminate() && !m_terminate_requested) {
      uint32_t next_pc;
      debug_trace_t trace;

      // Simulator routine call handling.
      // Simulator routines start at PC = 0xffff0000.
      if ((m_regs[REG_PC] & 0xffff0000u) == 0xffff0000u) {
        // Call the routine.
        const uint32_t routine_no = (m_regs[REG_PC] - 0xffff0000u) >> 2u;
        m_syscalls.call(routine_no, m_regs);

        // Simulate jmp lr.
        m_regs[REG_PC] = m_regs[REG_LR];
      }

      // IF/ID
      {
        // Read the instruction from the current PC.
        const uint32_t pc = m_regs[REG_PC];
        const uint32_t iword = m_ram.load32(pc);
        ++m_fetched_instr_count;

        // Detect encoding class (A, B, C, D or E).
        const bool op_class_B = ((iword & 0xfc00007cu) == 0x0000007cu);
        const bool op_class_A = ((iword & 0xfc000000u) == 0x00000000u) && !op_class_B;
        const bool op_class_E = ((iword & 0xfc000000u) == 0xdc000000u);
        const bool op_class_D = ((iword & 0xe0000000u) == 0xc0000000u) && !op_class_E;
        const bool op_class_C = !op_class_A && !op_class_B && !op_class_D && !op_class_E;

        // Is this a vector operation?
        const uint32_t vec_mask = op_class_A ? 3u : (op_class_B || op_class_C ? 2u : 0u);
        const uint32_t vector_mode = (iword >> 14u) & vec_mask;
        const bool is_vector_op = (vector_mode != 0u);
        const bool is_folding_vector_op = (vector_mode == 1u);

        // Is this a packed operation?
        const uint32_t packed_mode = (op_class_A || op_class_B) ? ((iword & 0x00000180u) >> 7) : 0u;

        // Extract parts of the instruction.
        // NOTE: These may or may not be valid, depending on the instruction type.
        const uint32_t reg1 = (iword >> 21u) & 31u;
        const uint32_t reg2 = (iword >> 16u) & 31u;
        const uint32_t reg3 = (iword >> 9u) & 31u;
        const uint32_t imm15 = decode_imm15(iword);
        const uint32_t imm18 = decode_imm18(iword);
        const uint32_t imm21 = decode_imm21(iword);

        // == BRANCH HANDLING ==

        const bool is_bcc = ((iword & 0xfc000000u) == 0xdc000000u);
        const bool is_j = ((iword & 0xf8000000u) == 0xc0000000u);
        const bool is_subroutine_branch = ((iword & 0xfc000000u) == 0xc4000000u);
        const bool is_branch = is_bcc || is_j;

        if (is_bcc) {
          // b[cc]: Evaluate condition (for b[cc]).
          bool branch_taken = false;
          const uint32_t branch_condition_value = m_regs[reg1];
          const uint32_t condition = (iword >> 18u) & 0x00000007u;
          switch (condition) {
            case 0:  // bz
              branch_taken = (branch_condition_value == 0u);
              break;
            case 1:  // bnz
              branch_taken = (branch_condition_value != 0u);
              break;
            case 2:  // bs
              branch_taken = (branch_condition_value == 0xffffffffu);
              break;
            case 3:  // bns
              branch_taken = (branch_condition_value != 0xffffffffu);
              break;
            case 4:  // blt
              branch_taken = ((branch_condition_value & 0x80000000u) != 0u);
              break;
            case 5:  // bge
              branch_taken = ((branch_condition_value & 0x80000000u) == 0u);
              break;
            case 6:  // ble
              branch_taken =
                  ((branch_condition_value & 0x80000000u) != 0u) || (branch_condition_value == 0u);
              break;
            case 7:  // bgt
              branch_taken =
                  ((branch_condition_value & 0x80000000u) == 0u) && (branch_condition_value != 0u);
              break;
          }
          next_pc = branch_taken ? (pc + imm18) : (pc + 4u);
        } else if (is_j) {
          // j/jl
          const uint32_t base_address = (reg1 == 31 ? pc : m_regs[reg1]);
          next_pc = base_address + imm21;

          // JL implicitly writes to LR (we do it here instead of putting it through the EX+WB
          // machinery).
          if (is_subroutine_branch) {
            m_regs[REG_LR] = pc + 4u;
          }
        } else {
          // No branch: Increment the PC by 4.
          next_pc = pc + 4u;
        }

        // == DECODE ==

        // Is this a mem load/store operation?
        const bool is_ldx =
            ((iword & 0xfc000078u) == 0x00000000u) && ((iword & 0x00000007u) != 0x00000000u);
        const bool is_ld =
            ((iword & 0xe0000000u) == 0x00000000u) && ((iword & 0x1c000000u) != 0x00000000u);
        const bool is_ldwpc = ((iword & 0xfc000000u) == 0xc8000000u);
        const bool is_mem_load = is_ldx || is_ld | is_ldwpc;
        const bool is_stx = ((iword & 0xfc000078u) == 0x00000008u);
        const bool is_st = ((iword & 0xe0000000u) == 0x20000000u);
        const bool is_stwpc = ((iword & 0xfc000000u) == 0xcc000000u);
        const bool is_mem_store = is_stx || is_st || is_stwpc;
        const bool is_mem_op = (is_mem_load || is_mem_store);

        // Is this ADDPC/ADDPCHI?
        const bool is_addpc_addpchi = ((iword & 0xf8000000u) == 0xd0000000u);

        // Is this a three-source-operand instruction? I.e. memory store or the 3 source-operand
        // group (opcode = [0x2c, 0x2f]).
        const bool is_3op_group =
            ((iword & 0xfc00007cu) == 0x0000002cu) || ((iword & 0xf0000000u) == 0xb0000000u);
        const bool is_three_src_op = is_mem_store || is_3op_group;

        // Should we use reg1 as a source (special case)?
        const bool reg1_is_src = is_three_src_op || is_branch;

        // Should we use reg2 as a source?
        const bool reg2_is_src = op_class_A || op_class_B || op_class_C;

        // Should we use reg3 as a source?
        const bool reg3_is_src = op_class_A;

        // Should we use reg1 as a destination?
        const bool reg1_is_dst = !(is_mem_store || is_branch);

        // Determine the source & destination register numbers.
        const uint32_t src_reg_a = ((is_ldwpc || is_stwpc || is_addpc_addpchi) ? REG_PC : reg2);
        const uint32_t src_reg_b = reg3;
        const uint32_t src_reg_c = reg1;
        const uint32_t dst_reg = (reg1_is_dst ? reg1 : REG_Z);

        // Determine EX operation.
        uint32_t ex_op = EX_OP_OR;
        if (op_class_A && ((iword & 0x000001f0u) != 0x00000000u)) {
          ex_op = iword & 0x0000007fu;
        } else if (op_class_B) {
          ex_op = ((iword >> 1) & 0x00003f00u) | (iword & 0x0000007fu);
        } else if (op_class_C && ((iword & 0xc0000000u) != 0x00000000u)) {
          ex_op = iword >> 26u;
        } else if (op_class_D) {
          switch ((iword >> 26) & 7) {
            case 4:  // addpc
              ex_op = EX_OP_ADDPC;
              break;
            case 5:  // addpchi
              ex_op = EX_OP_ADDPCHI;
              break;
            case 6:  // ldi
              ex_op = EX_OP_LDI;
              break;
          }
        }

        // Determine MEM operation.
        uint32_t mem_op = MEM_OP_NONE;
        if (is_mem_op) {
          if (is_ldwpc) {
            mem_op = MEM_OP_LOAD32;
          } else if (is_stwpc) {
            mem_op = MEM_OP_STORE32;
          } else if (op_class_A) {
            mem_op = iword & 0x0000007fu;
          } else {
            mem_op = iword >> 26u;
          }
        }

        // Check what type of registers should be used (vector or scalar).
        const bool reg1_is_vector = is_vector_op;
        const bool reg2_is_vector = is_vector_op && !is_mem_op;
        const bool reg3_is_vector = ((vector_mode & 1u) != 0u);

        // Output to the EX stage.
        decode.src_reg_a.no = src_reg_a;
        decode.src_reg_a.is_vector = reg2_is_vector;
        decode.src_reg_b.no = src_reg_b;
        decode.src_reg_b.is_vector = reg3_is_vector;
        decode.src_reg_c.no = src_reg_c;
        decode.src_reg_c.is_vector = reg1_is_vector;
        decode.dst_reg.no = dst_reg;
        decode.dst_reg.is_vector = is_vector_op;

        decode.src_imm = op_class_C ? imm15 : imm21;
        decode.src_b_is_imm = op_class_C || op_class_D;
        decode.src_b_is_stride = is_vector_op && is_mem_op && !decode.src_reg_b.is_vector;

        decode.ex_op = ex_op;
        decode.packed_mode = packed_mode;
        decode.mem_op = mem_op;

        // Vector operation parameters.

        // == VECTOR STATE INITIALIZATION ==

        vector.is_vector_op = is_vector_op;
        if (is_vector_op) {
          vector.vector_len =
              actual_vector_len(m_regs[REG_VL], NUM_VECTOR_ELEMENTS, is_folding_vector_op);
          vector.stride = op_class_C ? imm15 : m_regs[reg3];
          vector.addr_offset = 0u;
          vector.folding = is_folding_vector_op;
        }

        // Debug trace (part 1).
        if (m_enable_tracing) {
          trace.valid = true;
          trace.src_a_valid = reg2_is_src;
          trace.src_b_valid = reg3_is_src;
          trace.src_c_valid = reg1_is_src;
          trace.pc = pc;
        }
      }

      // The vector loop.
      const auto num_vector_loops = vector.is_vector_op ? vector.vector_len : 1;
      for (uint32_t vec_idx = 0u; vec_idx < num_vector_loops; ++vec_idx) {
        // Perf stats.
        m_perf_symbols.add_ref(m_regs[REG_PC]);

        // RF

        // Read from the register files.
        const auto reg_a_data = [this, vector, vec_idx, decode]() {
          if (decode.src_reg_a.is_vector) {
            const auto vector_idx_a = vector.folding ? (vector.vector_len + vec_idx) : vec_idx;
            return m_vregs[decode.src_reg_a.no][vector_idx_a];
          } else {
            return m_regs[decode.src_reg_a.no];
          }
        }();
        const auto reg_b_data = decode.src_reg_b.is_vector ? m_vregs[decode.src_reg_b.no][vec_idx]
                                                           : m_regs[decode.src_reg_b.no];
        const auto reg_c_data = decode.src_reg_c.is_vector ? m_vregs[decode.src_reg_c.no][vec_idx]
                                                           : m_regs[decode.src_reg_c.no];

        // Select source data.
        const auto src_a = reg_a_data;
        const auto src_b =
            (decode.src_b_is_stride ? vector.addr_offset
                                    : (decode.src_b_is_imm ? decode.src_imm : reg_b_data));
        const auto src_c = reg_c_data;

        // Debug trace (part 2).
        if (m_enable_tracing) {
          trace.src_a = src_a;
          trace.src_b = src_b;
          trace.src_c = src_c;
          append_debug_trace(trace);
        }

        // EX
        {
          uint32_t ex_result = 0u;

          // Do the operation.
          if (decode.mem_op != MEM_OP_NONE) {
            // AGU - Address Generation Unit.
            ex_result = src_a + src_b * index_scale_factor(decode.packed_mode);
          } else {
            switch (decode.ex_op) {
              case EX_OP_XCHGSR:
                ex_result = xchgsr(src_a, src_b, decode.src_reg_a.no == REG_Z);
                break;

              case EX_OP_ADDPC:
              case EX_OP_ADDPCHI:
                ex_result = src_a + src_b;
                break;
              case EX_OP_LDI:
                ex_result = src_b;
                break;

              case EX_OP_OR:
                switch (decode.packed_mode) {
                  default:
                    ex_result = src_a | src_b;
                    break;
                  case 1:
                    ex_result = src_a | (~src_b);
                    break;
                  case 2:
                    ex_result = (~src_a) | src_b;
                    break;
                  case 3:
                    ex_result = (~src_a) | (~src_b);
                }
                break;
              case EX_OP_AND:
                switch (decode.packed_mode) {
                  default:
                    ex_result = src_a & src_b;
                    break;
                  case 1:
                    ex_result = src_a & (~src_b);
                    break;
                  case 2:
                    ex_result = (~src_a) & src_b;
                    break;
                  case 3:
                    ex_result = (~src_a) & (~src_b);
                }
                break;
              case EX_OP_XOR:
                switch (decode.packed_mode) {
                  default:
                    ex_result = src_a ^ src_b;
                    break;
                  case 1:
                    ex_result = src_a ^ (~src_b);
                    break;
                  case 2:
                    ex_result = (~src_a) ^ src_b;
                    break;
                  case 3:
                    ex_result = (~src_a) ^ (~src_b);
                }
                break;

              case EX_OP_ADD:
                switch (decode.packed_mode) {
                  case PACKED_BYTE:
                    ex_result = add8x4(src_a, src_b);
                    break;
                  case PACKED_HALF_WORD:
                    ex_result = add16x2(src_a, src_b);
                    break;
                  default:
                    ex_result = add32(src_a, src_b);
                }
                break;
              case EX_OP_SUB:
                switch (decode.packed_mode) {
                  case PACKED_BYTE:
                    ex_result = sub8x4(src_a, src_b);
                    break;
                  case PACKED_HALF_WORD:
                    ex_result = sub16x2(src_a, src_b);
                    break;
                  default:
                    ex_result = sub32(src_a, src_b);
                }
                break;
              case EX_OP_SEQ:
                switch (decode.packed_mode) {
                  case PACKED_BYTE:
                    ex_result = set8x4(src_a, src_b, [](uint8_t a, uint8_t b) { return a == b; });
                    break;
                  case PACKED_HALF_WORD:
                    ex_result =
                        set16x2(src_a, src_b, [](uint16_t a, uint16_t b) { return a == b; });
                    break;
                  default:
                    ex_result = set32(src_a, src_b, [](uint32_t a, uint32_t b) { return a == b; });
                }
                break;
              case EX_OP_SNE:
                switch (decode.packed_mode) {
                  case PACKED_BYTE:
                    ex_result = set8x4(src_a, src_b, [](uint8_t a, uint8_t b) { return a != b; });
                    break;
                  case PACKED_HALF_WORD:
                    ex_result =
                        set16x2(src_a, src_b, [](uint16_t a, uint16_t b) { return a != b; });
                    break;
                  default:
                    ex_result = set32(src_a, src_b, [](uint32_t a, uint32_t b) { return a != b; });
                }
                break;
              case EX_OP_SLT:
                switch (decode.packed_mode) {
                  case PACKED_BYTE:
                    ex_result = set8x4(src_a, src_b, [](uint8_t a, uint8_t b) {
                      return static_cast<int8_t>(a) < static_cast<int8_t>(b);
                    });
                    break;
                  case PACKED_HALF_WORD:
                    ex_result = set16x2(src_a, src_b, [](uint16_t a, uint16_t b) {
                      return static_cast<int16_t>(a) < static_cast<int16_t>(b);
                    });
                    break;
                  default:
                    ex_result = set32(src_a, src_b, [](uint32_t a, uint32_t b) {
                      return static_cast<int32_t>(a) < static_cast<int32_t>(b);
                    });
                }
                break;
              case EX_OP_SLTU:
                switch (decode.packed_mode) {
                  case PACKED_BYTE:
                    ex_result = set8x4(src_a, src_b, [](uint8_t a, uint8_t b) { return a < b; });
                    break;
                  case PACKED_HALF_WORD:
                    ex_result = set16x2(src_a, src_b, [](uint16_t a, uint16_t b) { return a < b; });
                    break;
                  default:
                    ex_result = set32(src_a, src_b, [](uint32_t a, uint32_t b) { return a < b; });
                }
                break;
              case EX_OP_SLE:
                switch (decode.packed_mode) {
                  case PACKED_BYTE:
                    ex_result = set8x4(src_a, src_b, [](uint8_t a, uint8_t b) {
                      return static_cast<int8_t>(a) <= static_cast<int8_t>(b);
                    });
                    break;
                  case PACKED_HALF_WORD:
                    ex_result = set16x2(src_a, src_b, [](uint16_t a, uint16_t b) {
                      return static_cast<int16_t>(a) <= static_cast<int16_t>(b);
                    });
                    break;
                  default:
                    ex_result = set32(src_a, src_b, [](uint32_t a, uint32_t b) {
                      return static_cast<int32_t>(a) <= static_cast<int32_t>(b);
                    });
                }
                break;
              case EX_OP_SLEU:
                switch (decode.packed_mode) {
                  case PACKED_BYTE:
                    ex_result = set8x4(src_a, src_b, [](uint8_t a, uint8_t b) { return a <= b; });
                    break;
                  case PACKED_HALF_WORD:
                    ex_result =
                        set16x2(src_a, src_b, [](uint16_t a, uint16_t b) { return a <= b; });
                    break;
                  default:
                    ex_result = set32(src_a, src_b, [](uint32_t a, uint32_t b) { return a <= b; });
                }
                break;
              case EX_OP_MIN:
                switch (decode.packed_mode) {
                  case PACKED_BYTE:
                    ex_result = sel32(src_a, src_b, set8x4(src_a, src_b, [](uint8_t x, uint8_t y) {
                                        return static_cast<int8_t>(x) < static_cast<int8_t>(y);
                                      }));
                    break;
                  case PACKED_HALF_WORD:
                    ex_result =
                        sel32(src_a, src_b, set16x2(src_a, src_b, [](uint16_t x, uint16_t y) {
                                return static_cast<int16_t>(x) < static_cast<int16_t>(y);
                              }));
                    break;
                  default:
                    ex_result = sel32(src_a, src_b, set32(src_a, src_b, [](uint32_t x, uint32_t y) {
                                        return static_cast<int32_t>(x) < static_cast<int32_t>(y);
                                      }));
                }
                break;
              case EX_OP_MAX:
                switch (decode.packed_mode) {
                  case PACKED_BYTE:
                    ex_result = sel32(src_a, src_b, set8x4(src_a, src_b, [](uint8_t x, uint8_t y) {
                                        return static_cast<int8_t>(x) > static_cast<int8_t>(y);
                                      }));
                    break;
                  case PACKED_HALF_WORD:
                    ex_result =
                        sel32(src_a, src_b, set16x2(src_a, src_b, [](uint16_t x, uint16_t y) {
                                return static_cast<int16_t>(x) > static_cast<int16_t>(y);
                              }));
                    break;
                  default:
                    ex_result = sel32(src_a, src_b, set32(src_a, src_b, [](uint32_t x, uint32_t y) {
                                        return static_cast<int32_t>(x) > static_cast<int32_t>(y);
                                      }));
                }
                break;
              case EX_OP_MINU:
                switch (decode.packed_mode) {
                  case PACKED_BYTE:
                    ex_result = sel32(src_a, src_b, set8x4(src_a, src_b, [](uint8_t x, uint8_t y) {
                                        return x < y;
                                      }));
                    break;
                  case PACKED_HALF_WORD:
                    ex_result =
                        sel32(src_a, src_b, set16x2(src_a, src_b, [](uint16_t x, uint16_t y) {
                                return x < y;
                              }));
                    break;
                  default:
                    ex_result = sel32(src_a, src_b, set32(src_a, src_b, [](uint32_t x, uint32_t y) {
                                        return x < y;
                                      }));
                }
                break;
              case EX_OP_MAXU:
                switch (decode.packed_mode) {
                  case PACKED_BYTE:
                    ex_result = sel32(src_a, src_b, set8x4(src_a, src_b, [](uint8_t x, uint8_t y) {
                                        return x > y;
                                      }));
                    break;
                  case PACKED_HALF_WORD:
                    ex_result =
                        sel32(src_a, src_b, set16x2(src_a, src_b, [](uint16_t x, uint16_t y) {
                                return x > y;
                              }));
                    break;
                  default:
                    ex_result = sel32(src_a, src_b, set32(src_a, src_b, [](uint32_t x, uint32_t y) {
                                        return x > y;
                                      }));
                }
                break;
              case EX_OP_EBF:
                switch (decode.packed_mode) {
                  case PACKED_BYTE:
                    ex_result = ebf8x4(src_a, src_b);
                    break;
                  case PACKED_HALF_WORD:
                    ex_result = ebf16x2(src_a, src_b);
                    break;
                  default:
                    ex_result = ebf32(src_a, src_b);
                }
                break;
              case EX_OP_EBFU:
                switch (decode.packed_mode) {
                  case PACKED_BYTE:
                    ex_result = ebfu8x4(src_a, src_b);
                    break;
                  case PACKED_HALF_WORD:
                    ex_result = ebfu16x2(src_a, src_b);
                    break;
                  default:
                    ex_result = ebfu32(src_a, src_b);
                }
                break;
              case EX_OP_MKBF:
                switch (decode.packed_mode) {
                  case PACKED_BYTE:
                    ex_result = mkbf8x4(src_a, src_b);
                    break;
                  case PACKED_HALF_WORD:
                    ex_result = mkbf16x2(src_a, src_b);
                    break;
                  default:
                    ex_result = mkbf32(src_a, src_b);
                }
                break;
              case EX_OP_IBF:
                switch (decode.packed_mode) {
                  case PACKED_BYTE:
                    ex_result = ibf8x4(src_a, src_b, src_c);
                    break;
                  case PACKED_HALF_WORD:
                    ex_result = ibf16x2(src_a, src_b, src_c);
                    break;
                  default:
                    ex_result = ibf32(src_a, src_b, src_c);
                }
                break;
              case EX_OP_SHUF:
                ex_result = shuf32(src_a, src_b);
                break;
              case EX_OP_SEL:
                switch (decode.packed_mode) {
                  default:
                    ex_result = sel32(src_a, src_b, src_c);
                    break;
                  case 1:
                    ex_result = sel32(src_b, src_a, src_c);
                    break;
                  case 2:
                    ex_result = sel32(src_c, src_b, src_a);
                    break;
                  case 3:
                    ex_result = sel32(src_b, src_c, src_a);
                    break;
                }
                break;
              case EX_OP_CLZ:
                switch (decode.packed_mode) {
                  case PACKED_BYTE:
                    ex_result = clz8x4(src_a);
                    break;
                  case PACKED_HALF_WORD:
                    ex_result = clz16x2(src_a);
                    break;
                  default:
                    ex_result = clz32(src_a);
                }
                break;
              case EX_OP_POPCNT:
                switch (decode.packed_mode) {
                  case PACKED_BYTE:
                    ex_result = popcnt8x4(src_a);
                    break;
                  case PACKED_HALF_WORD:
                    ex_result = popcnt16x2(src_a);
                    break;
                  default:
                    ex_result = popcnt32(src_a);
                }
                break;
              case EX_OP_REV:
                switch (decode.packed_mode) {
                  case PACKED_BYTE:
                    ex_result = rev8x4(src_a);
                    break;
                  case PACKED_HALF_WORD:
                    ex_result = rev16x2(src_a);
                    break;
                  default:
                    ex_result = rev32(src_a);
                }
                break;
              case EX_OP_PACK:
                switch (decode.packed_mode) {
                  case PACKED_BYTE:
                    ex_result = pack8x4(src_a, src_b);
                    break;
                  case PACKED_HALF_WORD:
                    ex_result = pack16x2(src_a, src_b);
                    break;
                  default:
                    ex_result = pack32(src_a, src_b);
                }
                break;
              case EX_OP_PACKS:
                switch (decode.packed_mode) {
                  case PACKED_BYTE:
                    ex_result = packs8x4(src_a, src_b);
                    break;
                  case PACKED_HALF_WORD:
                    ex_result = packs16x2(src_a, src_b);
                    break;
                  default:
                    ex_result = packs32(src_a, src_b);
                }
                break;
              case EX_OP_PACKSU:
                switch (decode.packed_mode) {
                  case PACKED_BYTE:
                    ex_result = packsu8x4(src_a, src_b);
                    break;
                  case PACKED_HALF_WORD:
                    ex_result = packsu16x2(src_a, src_b);
                    break;
                  default:
                    ex_result = packsu32(src_a, src_b);
                }
                break;
              case EX_OP_PACKHI:
                switch (decode.packed_mode) {
                  case PACKED_BYTE:
                    ex_result = packhi8x4(src_a, src_b);
                    break;
                  case PACKED_HALF_WORD:
                    ex_result = packhi16x2(src_a, src_b);
                    break;
                  default:
                    ex_result = packhi32(src_a, src_b);
                }
                break;
              case EX_OP_PACKHIR:
                switch (decode.packed_mode) {
                  case PACKED_BYTE:
                    ex_result = packhir8x4(src_a, src_b);
                    break;
                  case PACKED_HALF_WORD:
                    ex_result = packhir16x2(src_a, src_b);
                    break;
                  default:
                    ex_result = packhir32(src_a, src_b);
                }
                break;
              case EX_OP_PACKHIUR:
                switch (decode.packed_mode) {
                  case PACKED_BYTE:
                    ex_result = packhiur8x4(src_a, src_b);
                    break;
                  case PACKED_HALF_WORD:
                    ex_result = packhiur16x2(src_a, src_b);
                    break;
                  default:
                    ex_result = packhiur32(src_a, src_b);
                }
                break;

              case EX_OP_ADDS:
                switch (decode.packed_mode) {
                  case PACKED_BYTE:
                    ex_result = saturating_op_8x4(
                        src_a, src_b, [](int16_t x, int16_t y) -> int16_t { return x + y; });
                    break;
                  case PACKED_HALF_WORD:
                    ex_result = saturating_op_16x2(
                        src_a, src_b, [](int32_t x, int32_t y) -> int32_t { return x + y; });
                    break;
                  default:
                    ex_result = saturating_op_32(
                        src_a, src_b, [](int64_t x, int64_t y) -> int64_t { return x + y; });
                }
                break;
              case EX_OP_ADDSU:
                switch (decode.packed_mode) {
                  case PACKED_BYTE:
                    ex_result = saturating_op_u8x4(
                        src_a, src_b, [](uint16_t x, uint16_t y) -> uint16_t { return x + y; });
                    break;
                  case PACKED_HALF_WORD:
                    ex_result = saturating_op_u16x2(
                        src_a, src_b, [](uint32_t x, uint32_t y) -> uint32_t { return x + y; });
                    break;
                  default:
                    ex_result = saturating_op_u32(
                        src_a, src_b, [](uint64_t x, uint64_t y) -> uint64_t { return x + y; });
                }
                break;
              case EX_OP_ADDH:
                switch (decode.packed_mode) {
                  case PACKED_BYTE:
                    ex_result = halving_op_8x4(
                        src_a, src_b, [](int16_t x, int16_t y) -> int16_t { return x + y; });
                    break;
                  case PACKED_HALF_WORD:
                    ex_result = halving_op_16x2(
                        src_a, src_b, [](int32_t x, int32_t y) -> int32_t { return x + y; });
                    break;
                  default:
                    ex_result = halving_op_32(
                        src_a, src_b, [](int64_t x, int64_t y) -> int64_t { return x + y; });
                }
                break;
              case EX_OP_ADDHU:
                switch (decode.packed_mode) {
                  case PACKED_BYTE:
                    ex_result = halving_op_u8x4(
                        src_a, src_b, [](uint16_t x, uint16_t y) -> uint16_t { return x + y; });
                    break;
                  case PACKED_HALF_WORD:
                    ex_result = halving_op_u16x2(
                        src_a, src_b, [](uint32_t x, uint32_t y) -> uint32_t { return x + y; });
                    break;
                  default:
                    ex_result = halving_op_u32(
                        src_a, src_b, [](uint64_t x, uint64_t y) -> uint64_t { return x + y; });
                }
                break;
              case EX_OP_ADDHR:
                switch (decode.packed_mode) {
                  case PACKED_BYTE:
                    ex_result = halving_op_8x4(
                        src_a, src_b, [](int16_t x, int16_t y) -> int16_t { return x + y + 1; });
                    break;
                  case PACKED_HALF_WORD:
                    ex_result = halving_op_16x2(
                        src_a, src_b, [](int32_t x, int32_t y) -> int32_t { return x + y + 1; });
                    break;
                  default:
                    ex_result = halving_op_32(
                        src_a, src_b, [](int64_t x, int64_t y) -> int64_t { return x + y + 1; });
                }
                break;
              case EX_OP_ADDHUR:
                switch (decode.packed_mode) {
                  case PACKED_BYTE:
                    ex_result = halving_op_u8x4(
                        src_a, src_b, [](uint16_t x, uint16_t y) -> uint16_t { return x + y + 1; });
                    break;
                  case PACKED_HALF_WORD:
                    ex_result = halving_op_u16x2(
                        src_a, src_b, [](uint32_t x, uint32_t y) -> uint32_t { return x + y + 1; });
                    break;
                  default:
                    ex_result = halving_op_u32(
                        src_a, src_b, [](uint64_t x, uint64_t y) -> uint64_t { return x + y + 1; });
                }
                break;
              case EX_OP_SUBS:
                switch (decode.packed_mode) {
                  case PACKED_BYTE:
                    ex_result = saturating_op_8x4(
                        src_a, src_b, [](int16_t x, int16_t y) -> int16_t { return x - y; });
                    break;
                  case PACKED_HALF_WORD:
                    ex_result = saturating_op_16x2(
                        src_a, src_b, [](int32_t x, int32_t y) -> int32_t { return x - y; });
                    break;
                  default:
                    ex_result = saturating_op_32(
                        src_a, src_b, [](int64_t x, int64_t y) -> int64_t { return x - y; });
                }
                break;
              case EX_OP_SUBSU:
                switch (decode.packed_mode) {
                  case PACKED_BYTE:
                    ex_result = saturating_op_u8x4(
                        src_a, src_b, [](uint16_t x, uint16_t y) -> uint16_t { return x - y; });
                    break;
                  case PACKED_HALF_WORD:
                    ex_result = saturating_op_u16x2(
                        src_a, src_b, [](uint32_t x, uint32_t y) -> uint32_t { return x - y; });
                    break;
                  default:
                    ex_result = saturating_op_u32(
                        src_a, src_b, [](uint64_t x, uint64_t y) -> uint64_t { return x - y; });
                }
                break;
              case EX_OP_SUBH:
                switch (decode.packed_mode) {
                  case PACKED_BYTE:
                    ex_result = halving_op_8x4(
                        src_a, src_b, [](int16_t x, int16_t y) -> int16_t { return x - y; });
                    break;
                  case PACKED_HALF_WORD:
                    ex_result = halving_op_16x2(
                        src_a, src_b, [](int32_t x, int32_t y) -> int32_t { return x - y; });
                    break;
                  default:
                    ex_result = halving_op_32(
                        src_a, src_b, [](int64_t x, int64_t y) -> int64_t { return x - y; });
                }
                break;
              case EX_OP_SUBHU:
                switch (decode.packed_mode) {
                  case PACKED_BYTE:
                    ex_result = halving_op_u8x4(
                        src_a, src_b, [](uint16_t x, uint16_t y) -> uint16_t { return x - y; });
                    break;
                  case PACKED_HALF_WORD:
                    ex_result = halving_op_u16x2(
                        src_a, src_b, [](uint32_t x, uint32_t y) -> uint32_t { return x - y; });
                    break;
                  default:
                    ex_result = halving_op_u32(
                        src_a, src_b, [](uint64_t x, uint64_t y) -> uint64_t { return x - y; });
                }
                break;
              case EX_OP_SUBHR:
                switch (decode.packed_mode) {
                  case PACKED_BYTE:
                    ex_result = halving_op_8x4(
                        src_a, src_b, [](int16_t x, int16_t y) -> int16_t { return x - y + 1; });
                    break;
                  case PACKED_HALF_WORD:
                    ex_result = halving_op_16x2(
                        src_a, src_b, [](int32_t x, int32_t y) -> int32_t { return x - y + 1; });
                    break;
                  default:
                    ex_result = halving_op_32(
                        src_a, src_b, [](int64_t x, int64_t y) -> int64_t { return x - y + 1; });
                }
                break;
              case EX_OP_SUBHUR:
                switch (decode.packed_mode) {
                  case PACKED_BYTE:
                    ex_result = halving_op_u8x4(
                        src_a, src_b, [](uint16_t x, uint16_t y) -> uint16_t { return x - y + 1; });
                    break;
                  case PACKED_HALF_WORD:
                    ex_result = halving_op_u16x2(
                        src_a, src_b, [](uint32_t x, uint32_t y) -> uint32_t { return x - y + 1; });
                    break;
                  default:
                    ex_result = halving_op_u32(
                        src_a, src_b, [](uint64_t x, uint64_t y) -> uint64_t { return x - y + 1; });
                }
                break;

              case EX_OP_MUL:
                switch (decode.packed_mode) {
                  case PACKED_BYTE:
                    ex_result = mul8x4(src_a, src_b);
                    break;
                  case PACKED_HALF_WORD:
                    ex_result = mul16x2(src_a, src_b);
                    break;
                  default:
                    ex_result = mul32(src_a, src_b);
                }
                break;
              case EX_OP_MULHI:
                switch (decode.packed_mode) {
                  case PACKED_BYTE:
                    ex_result = mulhi8x4(src_a, src_b);
                    break;
                  case PACKED_HALF_WORD:
                    ex_result = mulhi16x2(src_a, src_b);
                    break;
                  default:
                    ex_result = mulhi32(src_a, src_b);
                }
                break;
              case EX_OP_MULHIU:
                switch (decode.packed_mode) {
                  case PACKED_BYTE:
                    ex_result = mulhiu8x4(src_a, src_b);
                    break;
                  case PACKED_HALF_WORD:
                    ex_result = mulhiu16x2(src_a, src_b);
                    break;
                  default:
                    ex_result = mulhiu32(src_a, src_b);
                }
                break;
              case EX_OP_MULQ:
                switch (decode.packed_mode) {
                  case PACKED_BYTE:
                    ex_result = saturating_op_8x4(
                        src_a, src_b, [](int16_t x, int16_t y) -> int16_t { return (x * y) >> 7; });
                    break;
                  case PACKED_HALF_WORD:
                    ex_result =
                        saturating_op_16x2(src_a, src_b, [](int32_t x, int32_t y) -> int32_t {
                          return (x * y) >> 15;
                        });
                    break;
                  default:
                    ex_result = saturating_op_32(src_a, src_b, [](int64_t x, int64_t y) -> int64_t {
                      return (x * y) >> 31;
                    });
                }
                break;
              case EX_OP_MULQR:
                switch (decode.packed_mode) {
                  case PACKED_BYTE:
                    ex_result =
                        saturating_op_8x4(src_a, src_b, [](int16_t x, int16_t y) -> int16_t {
                          return (x * y + (1 << 6)) >> 7;
                        });
                    break;
                  case PACKED_HALF_WORD:
                    ex_result =
                        saturating_op_16x2(src_a, src_b, [](int32_t x, int32_t y) -> int32_t {
                          return (x * y + (1 << 14)) >> 15;
                        });
                    break;
                  default:
                    ex_result = saturating_op_32(src_a, src_b, [](int64_t x, int64_t y) -> int64_t {
                      return (x * y + (1 << 30)) >> 31;
                    });
                }
                break;

              case EX_OP_MADD:
                switch (decode.packed_mode) {
                  case PACKED_BYTE:
                    ex_result = madd8x4(src_a, src_b, src_c);
                    break;
                  case PACKED_HALF_WORD:
                    ex_result = madd16x2(src_a, src_b, src_c);
                    break;
                  default:
                    ex_result = madd32(src_a, src_b, src_c);
                }
                break;

              case EX_OP_DIV:
                switch (decode.packed_mode) {
                  case PACKED_BYTE:
                    ex_result = div8x4(src_a, src_b);
                    break;
                  case PACKED_HALF_WORD:
                    ex_result = div16x2(src_a, src_b);
                    break;
                  default:
                    ex_result = div32(src_a, src_b);
                }
                break;
              case EX_OP_DIVU:
                switch (decode.packed_mode) {
                  case PACKED_BYTE:
                    ex_result = divu8x4(src_a, src_b);
                    break;
                  case PACKED_HALF_WORD:
                    ex_result = divu16x2(src_a, src_b);
                    break;
                  default:
                    ex_result = divu32(src_a, src_b);
                }
                break;
              case EX_OP_REM:
                switch (decode.packed_mode) {
                  case PACKED_BYTE:
                    ex_result = rem8x4(src_a, src_b);
                    break;
                  case PACKED_HALF_WORD:
                    ex_result = rem16x2(src_a, src_b);
                    break;
                  default:
                    ex_result = rem32(src_a, src_b);
                }
                break;
              case EX_OP_REMU:
                switch (decode.packed_mode) {
                  case PACKED_BYTE:
                    ex_result = remu8x4(src_a, src_b);
                    break;
                  case PACKED_HALF_WORD:
                    ex_result = remu16x2(src_a, src_b);
                    break;
                  default:
                    ex_result = remu32(src_a, src_b);
                }
                break;

              case EX_OP_ITOF:
                switch (decode.packed_mode) {
                  case PACKED_BYTE:
                    ex_result = itof8x4(src_a, src_b);
                    break;
                  case PACKED_HALF_WORD:
                    ex_result = itof16x2(src_a, src_b);
                    break;
                  default:
                    ex_result = itof32(src_a, src_b);
                }
                break;
              case EX_OP_UTOF:
                switch (decode.packed_mode) {
                  case PACKED_BYTE:
                    ex_result = utof8x4(src_a, src_b);
                    break;
                  case PACKED_HALF_WORD:
                    ex_result = utof16x2(src_a, src_b);
                    break;
                  default:
                    ex_result = utof32(src_a, src_b);
                }
                break;
              case EX_OP_FTOI:
                switch (decode.packed_mode) {
                  case PACKED_BYTE:
                    ex_result = ftoi8x4(src_a, src_b);
                    break;
                  case PACKED_HALF_WORD:
                    ex_result = ftoi16x2(src_a, src_b);
                    break;
                  default:
                    ex_result = ftoi32(src_a, src_b);
                }
                break;
              case EX_OP_FTOU:
                switch (decode.packed_mode) {
                  case PACKED_BYTE:
                    ex_result = ftou8x4(src_a, src_b);
                    break;
                  case PACKED_HALF_WORD:
                    ex_result = ftou16x2(src_a, src_b);
                    break;
                  default:
                    ex_result = ftou32(src_a, src_b);
                }
                break;
              case EX_OP_FTOIR:
                switch (decode.packed_mode) {
                  case PACKED_BYTE:
                    ex_result = ftoir8x4(src_a, src_b);
                    break;
                  case PACKED_HALF_WORD:
                    ex_result = ftoir16x2(src_a, src_b);
                    break;
                  default:
                    ex_result = ftoir32(src_a, src_b);
                }
                break;
              case EX_OP_FTOUR:
                switch (decode.packed_mode) {
                  case PACKED_BYTE:
                    ex_result = ftour8x4(src_a, src_b);
                    break;
                  case PACKED_HALF_WORD:
                    ex_result = ftour16x2(src_a, src_b);
                    break;
                  default:
                    ex_result = ftour32(src_a, src_b);
                }
                break;
              case EX_OP_FPACK:
                switch (decode.packed_mode) {
                  case PACKED_BYTE:
                    // Nothing to do here!
                    break;
                  case PACKED_HALF_WORD:
                    ex_result = fpack16x2(src_a, src_b);
                    break;
                  default:
                    ex_result = fpack32(src_a, src_b);
                }
                break;
              case EX_OP_FADD:
                switch (decode.packed_mode) {
                  case PACKED_BYTE:
                    ex_result = fadd8x4(src_a, src_b);
                    break;
                  case PACKED_HALF_WORD:
                    ex_result = fadd16x2(src_a, src_b);
                    break;
                  default:
                    ex_result = fadd32(src_a, src_b);
                }
                break;
              case EX_OP_FSUB:
                switch (decode.packed_mode) {
                  case PACKED_BYTE:
                    ex_result = fsub8x4(src_a, src_b);
                    break;
                  case PACKED_HALF_WORD:
                    ex_result = fsub16x2(src_a, src_b);
                    break;
                  default:
                    ex_result = fsub32(src_a, src_b);
                }
                break;
              case EX_OP_FMUL:
                switch (decode.packed_mode) {
                  case PACKED_BYTE:
                    ex_result = fmul8x4(src_a, src_b);
                    break;
                  case PACKED_HALF_WORD:
                    ex_result = fmul16x2(src_a, src_b);
                    break;
                  default:
                    ex_result = fmul32(src_a, src_b);
                }
                break;
              case EX_OP_FDIV:
                switch (decode.packed_mode) {
                  case PACKED_BYTE:
                    ex_result = fdiv8x4(src_a, src_b);
                    break;
                  case PACKED_HALF_WORD:
                    ex_result = fdiv16x2(src_a, src_b);
                    break;
                  default:
                    ex_result = fdiv32(src_a, src_b);
                }
                break;
              case EX_OP_FSEQ:
                switch (decode.packed_mode) {
                  case PACKED_BYTE:
                    ex_result = f8x4_t(src_a).fseq(f8x4_t(src_b));
                    break;
                  case PACKED_HALF_WORD:
                    ex_result = f16x2_t(src_a).fseq(f16x2_t(src_b));
                    break;
                  default:
                    ex_result = set32(src_a, src_b, [](uint32_t a, uint32_t b) {
                      return as_f32(a) == as_f32(b);
                    });
                }
                break;
              case EX_OP_FSNE:
                switch (decode.packed_mode) {
                  case PACKED_BYTE:
                    ex_result = f8x4_t(src_a).fsne(f8x4_t(src_b));
                    break;
                  case PACKED_HALF_WORD:
                    ex_result = f16x2_t(src_a).fsne(f16x2_t(src_b));
                    break;
                  default:
                    ex_result = set32(src_a, src_b, [](uint32_t a, uint32_t b) {
                      return as_f32(a) != as_f32(b);
                    });
                }
                break;
              case EX_OP_FSLT:
                switch (decode.packed_mode) {
                  case PACKED_BYTE:
                    ex_result = f8x4_t(src_a).fsle(f8x4_t(src_b));
                    break;
                  case PACKED_HALF_WORD:
                    ex_result = f16x2_t(src_a).fslt(f16x2_t(src_b));
                    break;
                  default:
                    ex_result = set32(
                        src_a, src_b, [](uint32_t a, uint32_t b) { return as_f32(a) < as_f32(b); });
                }
                break;
              case EX_OP_FSLE:
                switch (decode.packed_mode) {
                  case PACKED_BYTE:
                    ex_result = f8x4_t(src_a).fsle(f8x4_t(src_b));
                    break;
                  case PACKED_HALF_WORD:
                    ex_result = f16x2_t(src_a).fsle(f16x2_t(src_b));
                    break;
                  default:
                    ex_result = set32(src_a, src_b, [](uint32_t a, uint32_t b) {
                      return as_f32(a) <= as_f32(b);
                    });
                }
                break;
              case EX_OP_FSUNORD:
                switch (decode.packed_mode) {
                  case PACKED_BYTE:
                    ex_result = f8x4_t(src_a).fsunord(f8x4_t(src_b));
                    break;
                  case PACKED_HALF_WORD:
                    ex_result = f16x2_t(src_a).fsunord(f16x2_t(src_b));
                    break;
                  default:
                    ex_result = set32(src_a, src_b, [](uint32_t a, uint32_t b) {
                      return float32_isnan(a) || float32_isnan(b);
                    });
                }
                break;
              case EX_OP_FSORD:
                switch (decode.packed_mode) {
                  case PACKED_BYTE:
                    ex_result = f8x4_t(src_a).fsord(f8x4_t(src_b));
                    break;
                  case PACKED_HALF_WORD:
                    ex_result = f16x2_t(src_a).fsord(f16x2_t(src_b));
                    break;
                  default:
                    ex_result = set32(src_a, src_b, [](uint32_t a, uint32_t b) {
                      return !float32_isnan(a) && !float32_isnan(b);
                    });
                }
                break;
              case EX_OP_FMIN:
                switch (decode.packed_mode) {
                  case PACKED_BYTE:
                    ex_result = fmin8x4(src_a, src_b);
                    break;
                  case PACKED_HALF_WORD:
                    ex_result = fmin16x2(src_a, src_b);
                    break;
                  default:
                    ex_result = fmin32(src_a, src_b);
                }
                break;
              case EX_OP_FMAX:
                switch (decode.packed_mode) {
                  case PACKED_BYTE:
                    ex_result = fmax8x4(src_a, src_b);
                    break;
                  case PACKED_HALF_WORD:
                    ex_result = fmax16x2(src_a, src_b);
                    break;
                  default:
                    ex_result = fmax32(src_a, src_b);
                }
                break;
              case EX_OP_FUNPL:
                switch (decode.packed_mode) {
                  case PACKED_BYTE:
                    // Nothing to do here.
                    break;
                  case PACKED_HALF_WORD:
                    ex_result = f16x2_t::from_f32x2(f8x4_t(src_a)[0], f8x4_t(src_a)[2]).packf();
                    break;
                  default:
                    ex_result = as_u32(f16x2_t(src_a)[0]);
                }
                break;
              case EX_OP_FUNPH:
                switch (decode.packed_mode) {
                  case PACKED_BYTE:
                    // Nothing to do here.
                    break;
                  case PACKED_HALF_WORD:
                    ex_result = f16x2_t::from_f32x2(f8x4_t(src_a)[1], f8x4_t(src_a)[3]).packf();
                    break;
                  default:
                    ex_result = as_u32(f16x2_t(src_a)[1]);
                }
                break;
              case EX_OP_FSQRT:
                switch (decode.packed_mode) {
                  case PACKED_BYTE:
                    ex_result = fsqrt8x4(src_a, src_b);
                    break;
                  case PACKED_HALF_WORD:
                    ex_result = fsqrt16x2(src_a, src_b);
                    break;
                  default:
                    ex_result = fsqrt32(src_a, src_b);
                }
                break;
              case EX_OP_WAIT:
                // Not much to do here.
                ex_result = 0U;
                break;
              case EX_OP_SYNC:
                // Not much to do here.
                ex_result = 0U;
                break;
              case EX_OP_CCTRL:
                // Not much to do here.
                ex_result = src_c;
                break;
              case EX_OP_CRC32C:
                switch (decode.packed_mode) {
                  default:
                    ex_result = crc32c_8(src_c, src_a);
                    break;
                  case 1:
                    ex_result = crc32c_16(src_c, src_a);
                    break;
                  case 2:
                    ex_result = crc32c_32(src_c, src_a);
                    break;
                }
                break;
              case EX_OP_CRC32:
                switch (decode.packed_mode) {
                  default:
                    ex_result = crc32_8(src_c, src_a);
                    break;
                  case 1:
                    ex_result = crc32_16(src_c, src_a);
                    break;
                  case 2:
                    ex_result = crc32_32(src_c, src_a);
                    break;
                }
                break;
            }
          }

          // MEM
          uint32_t mem_result = 0u;
          switch (decode.mem_op) {
            case MEM_OP_LOAD8:
              mem_result = m_ram.load8signed(ex_result);
              break;
            case MEM_OP_LOADU8:
              mem_result = m_ram.load8(ex_result);
              break;
            case MEM_OP_LOAD16:
              mem_result = m_ram.load16signed(ex_result);
              break;
            case MEM_OP_LOADU16:
              mem_result = m_ram.load16(ex_result);
              break;
            case MEM_OP_LOAD32:
              mem_result = m_ram.load32(ex_result);
              break;
            case MEM_OP_LDEA:
              mem_result = ex_result;
              break;
            case MEM_OP_STORE8:
              m_ram.store8(ex_result, src_c);
              break;
            case MEM_OP_STORE16:
              m_ram.store16(ex_result, src_c);
              break;
            case MEM_OP_STORE32:
              m_ram.store32(ex_result, src_c);
              break;
          }

          // WB
          if (decode.dst_reg.no != REG_Z) {
            const auto dst_data = (decode.mem_op != MEM_OP_NONE) ? mem_result : ex_result;
            if (decode.dst_reg.is_vector) {
              m_vregs[decode.dst_reg.no][vec_idx] = dst_data;
            } else {
              m_regs[decode.dst_reg.no] = dst_data;
            }
          }
        }

        // Do vector offset increments in the ID/RF stage.
        vector.addr_offset += vector.stride;

        ++m_total_cycle_count;
        if (max_cycles >= 0 && static_cast<int64_t>(m_total_cycle_count) >= max_cycles) {
          m_terminate_requested = true;
          break;
        }
        update_mc1_clkcnt();
      }

      if (vector.is_vector_op) {
        m_vector_loop_count += num_vector_loops;
      }

      // Update the PC.
      m_regs[REG_PC] = next_pc;
    }
  } catch (std::exception& e) {
    std::string dump("\n");
    for (int i = 1; i <= 26; ++i) {
      dump += "R" + as_dec(i) + ": " + as_hex32(m_regs[i]) + "\n";
    }
    dump += "TP: " + as_hex32(m_regs[REG_TP]) + "\n";
    dump += "FP: " + as_hex32(m_regs[REG_FP]) + "\n";
    dump += "SP: " + as_hex32(m_regs[REG_SP]) + "\n";
    dump += "LR: " + as_hex32(m_regs[REG_LR]) + "\n";
    dump += "VL: " + as_hex32(m_regs[REG_VL]) + "\n";
    dump += "PC: " + as_hex32(m_regs[REG_PC]) + "\n";
    throw std::runtime_error(e.what() + dump);
  }

  end_simulation();

  return m_syscalls.exit_code();
}

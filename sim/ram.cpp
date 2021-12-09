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

#include <ram.hpp>

#include <cstring>

#if !defined(_WIN32)
#include <sys/mman.h>
#endif

ram_t::ram_t(const uint64_t ram_size) : m_size(ram_size) {
#if defined(_WIN32)
  // TODO(m): Use MapViewOfFile() instead of malloc()?
  m_memory = static_cast<uint8_t*>(std::malloc(ram_size));
  if (m_memory == nullptr) {
    throw std::runtime_error("Out of memory");
  }
#else
  // Use mmap() to allocate the simulator memory. This has very low startup overhead, and pages are
  // "pulled in" on demand.
  const int prot = PROT_READ | PROT_WRITE;
  const int flags = MAP_PRIVATE | MAP_ANON;
  auto* ptr = ::mmap(nullptr, static_cast<size_t>(ram_size), prot, flags, -1, 0);
  if (ptr == MAP_FAILED) {
    throw std::runtime_error("mmap failed: " + std::string(std::strerror(errno)));
  }
  m_memory = static_cast<uint8_t*>(ptr);
#endif
}

ram_t::~ram_t() {
#if defined(_WIN32)
  if (m_memory != nullptr) {
    free(m_memory);
  }
#else
  if (m_memory != nullptr) {
    ::munmap(m_memory, static_cast<size_t>(m_size));
  }
#endif
}

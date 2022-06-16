// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.
#pragma once

#if defined(INSIDE_ENCLAVE) && !defined(VIRTUAL_ENCLAVE)
#  include <openenclave/bits/security.h>
#else
#  include <cstring>
#endif

#include <cstdint>

extern "C"
{
/*
 * Write value to host memory with barrier.
 *
 * The value requires explicit type-casted if it is constant.
 * Only standard types are allowed (e.g., the size of the type
 * should be 8-byte, 4-byte, 2-byte, or 1-byte). Otherwise, the
 * asm function would cause compiler errors.
 */
#define CCF_OE_WRITE_VALUE_WITH_BARRIER(dest, value) \
  do \
  { \
    uint16_t _ds; \
    asm volatile( \
      "movw %%ds, %0\n\t" \
      "verw %0\n\t" \
      "mov %2, %1\n\t" \
      "mfence\n\t" \
      "lfence\n\t" \
      : "=m"(_ds), "=m"(*(typeof(value)*)(dest)) \
      : "r"(value) \
      : "cc"); \
  } while (0)
}

#define ALIGNMENT_MINUS_ONE 7

static void _memcpy_aligned(void* dest, const void* src, size_t count)
{
  uint64_t rdi, rsi, rcx;
  asm volatile(
    "shr $3, %2\n\t"
    "rep movsq\n\t"
    : "=D"(rdi), "=S"(rsi), "=c"(rcx) /* rdi, rsi, and rcx are clobbered */
    : "D"(dest), "S"(src), "c"(count)
    : "memory");
}

static void* _memcpy_unaligned_with_barrier(
  void* dest, const void* src, size_t count)
{
  uint64_t dest_addr = (uint64_t)dest;
  uint64_t src_addr = (uint64_t)src;

  while (count >= 8)
  {
    if (dest_addr % 8 == 0)
    {
      /* for 8-byte-aligned memory, use regular memcpy with the size being
       * the multiples of 8 */
      size_t count_aligned = count - count % 8;
      _memcpy_aligned((void*)dest_addr, (const void*)src_addr, count_aligned);
      src_addr += count_aligned;
      dest_addr += count_aligned;
      count -= count_aligned;
    }
    else
    {
      /* for non-8-byte-aligned memory, use the greedy approach to find
       * the optimized-size write with barrier */
      uint64_t next_aligned_addr =
        (dest_addr + ALIGNMENT_MINUS_ONE) & ~(uint64_t)ALIGNMENT_MINUS_ONE;
      uint64_t gap_count;

      while ((gap_count = next_aligned_addr - dest_addr))
      {
        if (gap_count >= 4)
        {
          CCF_OE_WRITE_VALUE_WITH_BARRIER(
            (void*)dest_addr, *(uint32_t*)src_addr);
          dest_addr += 4;
          src_addr += 4;
          count -= 4;
        }
        else if (gap_count >= 2)
        {
          CCF_OE_WRITE_VALUE_WITH_BARRIER(
            (void*)dest_addr, *(uint16_t*)src_addr);
          dest_addr += 2;
          src_addr += 2;
          count -= 2;
        }
        else
        {
          CCF_OE_WRITE_VALUE_WITH_BARRIER(
            (void*)dest_addr, *(uint8_t*)src_addr);
          dest_addr++;
          src_addr++;
          count--;
        }
      }
    }
  }

  /* use the greedy approach to find the optimized-size write with barrier for
   * the reset of the memory */
  while (count)
  {
    if (count >= 4)
    {
      CCF_OE_WRITE_VALUE_WITH_BARRIER((void*)dest_addr, *(uint32_t*)src_addr);
      dest_addr += 4;
      src_addr += 4;
      count -= 4;
    }
    else if (count >= 2)
    {
      CCF_OE_WRITE_VALUE_WITH_BARRIER((void*)dest_addr, *(uint16_t*)src_addr);
      dest_addr += 2;
      src_addr += 2;
      count -= 2;
    }
    else
    {
      CCF_OE_WRITE_VALUE_WITH_BARRIER((void*)dest_addr, *(uint8_t*)src_addr);
      dest_addr++;
      src_addr++;
      count--;
    }
  }

  return dest;
}

static inline void* ccf_memcpy(void* dest, const void* src, size_t count)
{
#if defined(INSIDE_ENCLAVE) && !defined(VIRTUAL_ENCLAVE)
  return oe_memcpy_with_barrier(dest, src, count);
#else
  /* If both dest and count are 8-byte aligned, fallback to regular
   * (fast) memcpy. For the other cases, use the hardened version of memcpy.
   * Note that the hardened memcpy should not be inline, otherwise the
   * fence instructions in branches will slowdown the fallback path. */
  if (((uint64_t)dest % 8 == 0) && (count % 8 == 0))
    _memcpy_aligned(dest, src, count);
  else
    _memcpy_unaligned_with_barrier(dest, src, count);

  return dest;
#endif
}

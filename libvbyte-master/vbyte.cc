/*
 * Copyright (C) 2005-2016 Christoph Rupp (chris@crupp.de).
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include <assert.h>
#include <string.h>
#include <cstdio>
#include <stdlib.h>
#include <iostream>

#include <omp.h>

#if defined(_MSC_VER) && _MSC_VER < 1600
typedef unsigned int uint32_t;
typedef unsigned char uint8_t;
typedef signed char int8_t;
#else
#  include <stdint.h>
#endif

#if defined(__AVX__) || defined(__AVX2__) || defined(__SSE4__)
#  define USE_MASKEDVBYTE 1
#endif

#include "vbyte.h"

namespace vbyte {

#ifdef __SSE2__

// AVX might be enabled at compile time, but it's still possible that
// it's not available at run-time because the CPU is an older model.

// from http://stackoverflow.com/questions/6121792/how-to-check-if-a-cpu-supports-the-sse3-instruction-set

#ifdef _WIN32
//  Windows
#  include <intrin.h>
#  define cpuid    __cpuid
#else
//  GCC Inline Assembly
static void
cpuid(int cpuinfo[4], int infotype) {
  __asm__ __volatile__ (
      "cpuid":
      "=a" (cpuinfo[0]),
      "=b" (cpuinfo[1]),
      "=c" (cpuinfo[2]),
      "=d" (cpuinfo[3]) :
      "a" (infotype)
  );
}
#endif

static inline bool
is_avx_available()
{
  static bool available = false;
  static bool initialized = false;
  if (!initialized) {
    initialized = true;

    int info[4];
    cpuid(info, 0);
    int num_ids = info[0];
    cpuid(info, 0x80000000);

    //  Detect Instruction Set
    if (num_ids >= 1) {
      cpuid(info, 0x00000001);
      available = (info[2] & ((int)1 << 28)) != 0;
    }
  }

  return available;
}
#else
static inline bool
is_avx_available()
{
  return false;
}
#endif

static inline int
read_int(const uint8_t *in, uint32_t *out)
{
  //std::cout << "read int32" << std::endl;

  *out = in[0] & 0x7Fu;
  if (in[0] < 128)
    return 1;
  *out = ((in[1] & 0x7Fu) << 7) | *out;
  if (in[1] < 128)
    return 2;
  *out = ((in[2] & 0x7Fu) << 14) | *out;
  if (in[2] < 128)
    return 3;
  *out = ((in[3] & 0x7Fu) << 21) | *out;
  if (in[3] < 128)
    return 4;
  *out = ((in[4] & 0x7Fu) << 28) | *out;
  return 5;
}

static inline int
read_int(const uint8_t *in, uint64_t *out)
{
  *out = in[0] & 0x7Fu;
  if (in[0] < 128){
    return 1;
  }
  *out = ((in[1] & 0x7Fu) << 7) | *out;
  if (in[1] < 128){
    return 2;
  }
  *out = ((in[2] & 0x7Fu) << 14) | *out;
  if (in[2] < 128){
    return 3;
  }
  *out = ((in[3] & 0x7Fu) << 21) | *out;
  if (in[3] < 128){
    return 4;
  }
  *out = ((uint64_t)(in[4] & 0x7Fu) << 28) | *out;
  if (in[4] < 128){
    return 5;
  }
  *out = ((uint64_t)(in[5] & 0x7Fu) << 35) | *out;
  if (in[5] < 128){
    return 6;
  }
  *out = ((uint64_t)(in[6] & 0x7Fu) << 42) | *out;
  if (in[6] < 128){
    return 7;
  }
  *out = ((uint64_t)(in[7] & 0x7Fu) << 49) | *out;
  if (in[7] < 128){
    return 8;
  }
  *out = ((uint64_t)(in[8] & 0x7Fu) << 56) | *out;
  if (in[8] < 128){
    return 9;
  }
  *out = ((uint64_t)(in[9] & 0x7Fu) << 63) | *out;
  return 10;
}

static inline int
write_int(uint8_t *p, uint32_t value)
{
  if (value < (1u << 7)) {
    *p = value & 0x7Fu;
    return 1;
  }
  if (value < (1u << 14)) {
    *p = (value & 0x7Fu) | (1u << 7);
    ++p;
    *p = value >> 7;
    return 2;
  }
  if (value < (1u << 21)) {
    *p = (value & 0x7Fu) | (1u << 7);
    ++p;
    *p = ((value >> 7) & 0x7Fu) | (1u << 7);
    ++p;
    *p = value >> 14;
    return 3;
  }
  if (value < (1u << 28)) {
    *p = (value & 0x7Fu) | (1u << 7);
    ++p;
    *p = ((value >> 7) & 0x7Fu) | (1u << 7);
    ++p;
    *p = ((value >> 14) & 0x7Fu) | (1u << 7);
    ++p;
    *p = value >> 21;
    return 4;
  }
  else {
    *p = (value & 0x7Fu) | (1u << 7);
    ++p;
    *p = ((value >> 7) & 0x7Fu) | (1u << 7);
    ++p;
    *p = ((value >> 14) & 0x7Fu) | (1u << 7);
    ++p;
    *p = ((value >> 21) & 0x7Fu) | (1u << 7);
    ++p;
    *p = value >> 28;
    return 5;
  }
}

static inline int
write_int(uint8_t *p, uint64_t value)
{
  if (value < (1lu << 7)) {
    *p = value & 0x7Fu;
    return 1;
  }
  if (value < (1lu << 14)) {
    *p = (value & 0x7Fu) | (1u << 7);
    ++p;
    *p = value >> 7;
    return 2;
  }
  if (value < (1lu << 21)) {
    *p = (value & 0x7Fu) | (1u << 7);
    ++p;
    *p = ((value >> 7) & 0x7Fu) | (1u << 7);
    ++p;
    *p = value >> 14;
    return 3;
  }
  if (value < (1lu << 28)) {
    *p = (value & 0x7Fu) | (1u << 7);
    ++p;
    *p = ((value >> 7) & 0x7Fu) | (1u << 7);
    ++p;
    *p = ((value >> 14) & 0x7Fu) | (1u << 7);
    ++p;
    *p = value >> 21;
    return 4;
  }
  if (value < (1lu << 35)) {
    *p = (value & 0x7Fu) | (1u << 7);
    ++p;
    *p = ((value >> 7) & 0x7Fu) | (1u << 7);
    ++p;
    *p = ((value >> 14) & 0x7Fu) | (1u << 7);
    ++p;
    *p = ((value >> 21) & 0x7Fu) | (1u << 7);
    ++p;
    *p = value >> 28;
    return 5;
  }
  if (value < (1lu << 42)) {
    *p = (value & 0x7Fu) | (1u << 7);
    ++p;
    *p = ((value >> 7) & 0x7Fu) | (1u << 7);
    ++p;
    *p = ((value >> 14) & 0x7Fu) | (1u << 7);
    ++p;
    *p = ((value >> 21) & 0x7Fu) | (1u << 7);
    ++p;
    *p = ((value >> 28) & 0x7Fu) | (1u << 7);
    ++p;
    *p = value >> 35;
    return 6;
  }
  if (value < (1lu << 49)) {
    *p = (value & 0x7Fu) | (1u << 7);
    ++p;
    *p = ((value >> 7) & 0x7Fu) | (1u << 7);
    ++p;
    *p = ((value >> 14) & 0x7Fu) | (1u << 7);
    ++p;
    *p = ((value >> 21) & 0x7Fu) | (1u << 7);
    ++p;
    *p = ((value >> 28) & 0x7Fu) | (1u << 7);
    ++p;
    *p = ((value >> 35) & 0x7Fu) | (1u << 7);
    ++p;
    *p = value >> 42;
    return 7;
  }
  if (value < (1lu << 56)) {
    *p = (value & 0x7Fu) | (1u << 7);
    ++p;
    *p = ((value >> 7) & 0x7Fu) | (1u << 7);
    ++p;
    *p = ((value >> 14) & 0x7Fu) | (1u << 7);
    ++p;
    *p = ((value >> 21) & 0x7Fu) | (1u << 7);
    ++p;
    *p = ((value >> 28) & 0x7Fu) | (1u << 7);
    ++p;
    *p = ((value >> 35) & 0x7Fu) | (1u << 7);
    ++p;
    *p = ((value >> 42) & 0x7Fu) | (1u << 7);
    ++p;
    *p = value >> 49;
    return 8;
  }
  if (value < (1lu << 63)) {
    *p = (value & 0x7Fu) | (1u << 7);
    ++p;
    *p = ((value >> 7) & 0x7Fu) | (1u << 7);
    ++p;
    *p = ((value >> 14) & 0x7Fu) | (1u << 7);
    ++p;
    *p = ((value >> 21) & 0x7Fu) | (1u << 7);
    ++p;
    *p = ((value >> 28) & 0x7Fu) | (1u << 7);
    ++p;
    *p = ((value >> 35) & 0x7Fu) | (1u << 7);
    ++p;
    *p = ((value >> 42) & 0x7Fu) | (1u << 7);
    ++p;
    *p = ((value >> 49) & 0x7Fu) | (1u << 7);
    ++p;
    *p = value >> 56;
    return 9;
  }
  else {
    *p = (value & 0x7Fu) | (1u << 7);
    ++p;
    *p = ((value >> 7) & 0x7Fu) | (1u << 7);
    ++p;
    *p = ((value >> 14) & 0x7Fu) | (1u << 7);
    ++p;
    *p = ((value >> 21) & 0x7Fu) | (1u << 7);
    ++p;
    *p = ((value >> 28) & 0x7Fu) | (1u << 7);
    ++p;
    *p = ((value >> 35) & 0x7Fu) | (1u << 7);
    ++p;
    *p = ((value >> 42) & 0x7Fu) | (1u << 7);
    ++p;
    *p = ((value >> 49) & 0x7Fu) | (1u << 7);
    ++p;
    *p = ((value >> 56) & 0x7Fu) | (1u << 7);
    ++p;
    *p = value >> 63;
    return 10;
  }
}

static inline int
compressed_size(uint32_t value)
{
  if (value < (1u << 7))
    return 1;
  if (value < (1u << 14))
    return 2;
  if (value < (1u << 21))
    return 3;
  if (value < (1u << 28))
    return 4;
  return 5;
}

static inline int
compressed_size(uint64_t value)
{
  if (value < (1lu << 7))
    return 1;
  if (value < (1lu << 14))
    return 2;
  if (value < (1lu << 21))
    return 3;
  if (value < (1lu << 28))
    return 4;
  if (value < (1lu << 35))
    return 5;
  if (value < (1lu << 42))
    return 6;
  if (value < (1lu << 49))
    return 7;
  if (value < (1lu << 56))
    return 8;
  if (value < (1lu << 63))
    return 9;
  return 10;
}

template<typename T>
static inline size_t
compressed_size_sorted(const T *in, size_t length, T previous)
{
  size_t size = 0;
  const T *end = in + length;

  for (; in < end; in++) {
    size += compressed_size(*in - previous);
    previous = *in;
  }

  return size;
}

template<typename T>
static inline size_t
compressed_size_unsorted(const T *in, size_t length)
{
  size_t size = 0;
  const T *end = in + length;

  for (; in < end; in++)
    size += compressed_size(*in);
  return size;
}

template<typename T>
static inline size_t
compress_unsorted(const T *in, uint8_t *out, size_t length)
{
  const uint8_t *initial_out = out;
  const T *end = in + length;

  while (in < end) {
    out += write_int(out, *in);
    ++in;
  }
  return out - initial_out;
}

template<typename T>
static inline size_t
uncompress_unsorted(const uint8_t *in, T *out, size_t length)
{
  const uint8_t *initial_in = in;

  for (size_t i = 0; i < length; i++) {
    in += read_int(in, out);
    ++out;
  }
  return in - initial_in;
}

template<typename T>
static inline size_t
compress_sorted(const T *in, uint8_t *out, T previous, size_t length)
{
  const uint8_t *initial_out = out;
  const T *end = in + length;

  while (in < end) {
    out += write_int(out, *in - previous);
    previous = *in;
    ++in;
  }
  return out - initial_out;
}

template<typename T>
static inline size_t
uncompress_sorted(const uint8_t *in, T *out, T previous, size_t length)
{
  const uint8_t *initial_in = in;

  for (size_t i = 0; i < length; i++) {
    T current;
    in += read_int(in, &current);
    previous += current;
    *out = previous;
    ++out;
  }
  return in - initial_in;
}


/* used for both 32 and 64 bit integers */
template<typename T>
static inline size_t
getNextInterval(const uint8_t **in, T &previous, T &start, T &end)
{
  const uint8_t *initial_in = *in;

  //current is the d 
  T current;
  //start
  (*in) += read_int((*in), &current);
  //previous + current is the actual interval value
  //printf("current: %" PRIu32 "\n",current);
  previous += current;
  start = previous;

  //end
  (*in) += read_int((*in), &current);
  //printf("current: %" PRIu32 "\n",current);
  //previous + current is the actual interval value
  previous += current;
  end = previous;

  //printf(" [%" PRIu32 ",%" PRIu32 ")\n",start, end);
  //std::cout << "[" << start << "," << end << ")" << std::endl;

  return (*in) - initial_in;
}

/* used for both 32 and 64 bit integers */
static inline size_t
getNextInterval64(const uint8_t **in, uint64_t &previous, uint64_t &start, uint64_t &end)
{
  const uint8_t *initial_in = *in;

  //current is the d 
  uint64_t current;
  //start
  (*in) += read_int((*in), &current);
  //previous + current is the actual interval value
  //printf("current: %" PRIu32 "\n",current);
  previous += current;
  start = previous;

  //end
  (*in) += read_int((*in), &current);
  //printf("current: %" PRIu32 "\n",current);
  //previous + current is the actual interval value
  previous += current;
  end = previous;

  //printf(" [%" PRIu32 ",%" PRIu32 ")\n",start, end);
  //std::cout << " [" << start << "," << end << ")" << std::endl;

  return (*in) - initial_in;
}

template<typename T>
static inline T
select_sorted(const uint8_t *in, T previous, size_t index)
{
  for (size_t i = 0; i <= index; i++) {
    T current;
    in += read_int(in, &current);
    previous += current;
  }
  return previous;
}

template<typename T>
static inline T
select_unsorted(const uint8_t *in, size_t index)
{
  T value = 0;

  for (size_t i = 0; i <= index; i++)
    in += read_int(in, &value);
  return value;
}

template<typename T>
static inline size_t
search_unsorted(const uint8_t *in, size_t length, T value)
{
  T v;

  for (size_t i = 0; i < length; i++) {
    in += read_int(in, &v);
    if (v == value)
      return i;
  }
  return length;
}

template<typename T>
static inline size_t
sorted_search(const uint8_t *in, size_t length, T value, T previous, T *actual)
{
  T v;

  for (size_t i = 0; i < length; i++) {
    in += read_int(in, &v);
    previous += v;
    if (previous >= value) {
      *actual = previous;
      return i;
    }
  }
  return length;
}

} // namespace vbyte

size_t
vbyte_compressed_size_sorted32(const uint32_t *in, size_t length,
                uint32_t previous)
{
  return vbyte::compressed_size_sorted(in, length, previous);
}

size_t
vbyte_compressed_size_sorted64(const uint64_t *in, size_t length,
                uint64_t previous)
{
  return vbyte::compressed_size_sorted(in, length, previous);
}

size_t
vbyte_compressed_size_unsorted32(const uint32_t *in, size_t length)
{
  return vbyte::compressed_size_unsorted(in, length);
}

size_t
vbyte_compressed_size_unsorted64(const uint64_t *in, size_t length)
{
  return vbyte::compressed_size_unsorted(in, length);
}

size_t
vbyte_compress_unsorted32(const uint32_t *in, uint8_t *out, size_t length)
{
  return vbyte::compress_unsorted(in, out, length);
}

size_t
vbyte_compress_unsorted64(const uint64_t *in, uint8_t *out, size_t length)
{
  return vbyte::compress_unsorted(in, out, length);
}

size_t
vbyte_uncompress_unsorted32(const uint8_t *in, uint32_t *out, size_t length)
{
#if defined(USE_MASKEDVBYTE)
  if (vbyte::is_avx_available())
    return masked_vbyte_decode(in, out, (uint64_t)length);
#endif
  return vbyte::uncompress_unsorted(in, out, length);
}

size_t
vbyte_uncompress_unsorted64(const uint8_t *in, uint64_t *out, size_t length)
{
  return vbyte::uncompress_unsorted(in, out, length);
}

size_t
vbyte_compress_sorted32(const uint32_t *in, uint8_t *out, uint32_t previous,
                size_t length)
{
  return vbyte::compress_sorted(in, out, previous, length);
}

size_t
vbyte_compress_sorted64(const uint64_t *in, uint8_t *out, uint64_t previous,
                size_t length)
{
  return vbyte::compress_sorted(in, out, previous, length);
}

size_t
vbyte_uncompress_sorted32(const uint8_t *in, uint32_t *out, uint32_t previous,
                size_t length)
{
#if defined(USE_MASKEDVBYTE)
  if (vbyte::is_avx_available())
    return masked_vbyte_decode_delta(in, out, (uint64_t)length, previous);
#endif
  return vbyte::uncompress_sorted(in, out, previous, length);
}

size_t
vbyte_uncompress_sorted64(const uint8_t *in, uint64_t *out, uint64_t previous,
                size_t length)
{
  return vbyte::uncompress_sorted(in, out, previous, length);
}

uint32_t
vbyte_select_sorted32(const uint8_t *in, size_t size, uint32_t previous,
                size_t index)
{
  (void)size;
#if defined(USE_MASKEDVBYTE)
  if (vbyte::is_avx_available())
    return masked_vbyte_select_delta(in, (uint64_t)size, previous, index);
#endif
  return vbyte::select_sorted<uint32_t>(in, previous, index);
}

uint64_t
vbyte_select_sorted64(const uint8_t *in, size_t size, uint64_t previous,
                size_t index)
{
  (void)size;
  return vbyte::select_sorted<uint64_t>(in, previous, index);
}

uint32_t
vbyte_select_unsorted32(const uint8_t *in, size_t size, size_t index)
{
  (void)size;
  return vbyte::select_unsorted<uint32_t>(in, index);
}

uint64_t
vbyte_select_unsorted64(const uint8_t *in, size_t size, size_t index)
{
  (void)size;
  return vbyte::select_unsorted<uint64_t>(in, index);
}

size_t
vbyte_search_unsorted32(const uint8_t *in, size_t length, uint32_t value)
{
  return vbyte::search_unsorted(in, length, value);
}

size_t
vbyte_search_unsorted64(const uint8_t *in, size_t length, uint64_t value)
{
  return vbyte::search_unsorted(in, length, value);
}

size_t
vbyte_search_lower_bound_sorted32(const uint8_t *in, size_t length,
                uint32_t value, uint32_t previous, uint32_t *actual)
{
#if defined(USE_MASKEDVBYTE)
  if (vbyte::is_avx_available())
    return (size_t)masked_vbyte_search_delta(in, (uint64_t)length, previous,
                  value, actual);
#endif
  return vbyte::sorted_search(in, length, value, previous, actual);
}

size_t
vbyte_search_lower_bound_sorted64(const uint8_t *in, size_t length,
                uint64_t value, uint64_t previous, uint64_t *actual)
{
  return vbyte::sorted_search(in, length, value, previous, actual);
}

size_t
vbyte_append_sorted32(uint8_t *end, uint32_t previous, uint32_t value)
{
  assert(value > previous);
  return vbyte::write_int(end, value - previous);
}

size_t
vbyte_append_sorted64(uint8_t *end, uint64_t previous, uint64_t value)
{
  assert(value > previous);
  return vbyte::write_int(end, value - previous);
}

size_t
vbyte_append_unsorted32(uint8_t *end, uint32_t value)
{
  return vbyte::write_int(end, value);
}

size_t
vbyte_append_unsorted64(uint8_t *end, uint64_t value)
{
  return vbyte::write_int(end, value);
}

/* CUSTOM FUNCTIONS */

/* 32 bit */
int vbyte_join_compressed_sorted32(const uint8_t *inA, size_t lengthA, const uint8_t *inB, size_t lengthB){
  if (lengthA == 0 || lengthB == 0) {
    return 0;
  }
  //Completely ignored AVX method
  uint32_t startA, endA, startB, endB;

  size_t currentIntervalA = 0;
  size_t currentIntervalB = 0;

  uint32_t previousA = 0;
  uint32_t previousB = 0;

  // std::cout << lengthA << " intervals in A" << std::endl;  
  // std::cout << lengthB << " intervals in B" << std::endl;

  //get first interval
  //std::cout << "interval A (" << currentIntervalA << "): " << std::endl;
  vbyte::getNextInterval(&inA, previousA, startA, endA);
  currentIntervalA += 1;
  //std::cout << "interval B (" << currentIntervalB << "): " << std::endl;
  vbyte::getNextInterval(&inB, previousB, startB, endB);
  currentIntervalB += 1;

  do {
    // std::cout << "intervals: " << currentIntervalA << " and " << currentIntervalB << std::endl;
    // std::cout << "[" << startA << "," << endA << ") with [" << startB << "," << endB << ")" << std::endl;
    if (startA<=startB)
    {
      if (endA>startB) // overlap, endA>=startB if intervals are [s,e] and endA>startB if intervals are [s,e)
      {
        //they overlap, return 1
        // std::cout << "Overlap: " << startA << "," << endA << " and " << startB << "," << endB << std::endl;
        // std::cout << "  checked " << currentIntervalA << " and " << currentIntervalB << " intervals and found a HIT." << std::endl;
        return 1;
      } 
      else
      {
        //std::cout << "interval A (" << currentIntervalA << "): " << std::endl;
        vbyte::getNextInterval(&inA, previousA, startA, endA);
        currentIntervalA += 1;
      }
    }
    else // startB<startA
    {
      if (endB>startA) // overlap, endB>=startA if intervals are [s,e] and endB>startA if intervals are [s,e)
      {

        //they overlap, return 1
        // std::cout << "Overlap: " << startA << "," << endA << " and " << startB << "," << endB << std::endl;
        // std::cout << "  checked " << currentIntervalA << " and " << currentIntervalB << " intervals and found a HIT." << std::endl;
        return 1;
      }
      else
      {
        //std::cout << "interval B (" << currentIntervalB << "): " << std::endl;
        vbyte::getNextInterval(&inB, previousB, startB, endB);
        currentIntervalB += 1;
      }
    }


  } while(currentIntervalA <= lengthA && currentIntervalB <= lengthB);

  // std::cout << "  checked " << currentIntervalA-1 << " and " << currentIntervalB-1 << " intervals and DIDNT find a hit." << std::endl;
  return 0;
}


int vbyte_join_compressed_sorted32_hybrid(const uint8_t *inA, size_t lengthA, const uint8_t *inB, size_t lengthB){
  if (lengthA == 0 || lengthB == 0) {
    return 0;
  }
  //Completely ignored AVX method
  uint32_t startA, endA, startB, endB;

  size_t currentIntervalA = 0;
  size_t currentIntervalB = 0;

  uint32_t previousA = 0;
  uint32_t previousB = 0;

  // std::cout << lengthA << " intervals in A" << std::endl;  
  // std::cout << lengthB << " intervals in B" << std::endl;

  //get first interval
  //std::cout << "interval A (" << currentIntervalA << "): " << std::endl;
  vbyte::getNextInterval(&inA, previousA, startA, endA);
  currentIntervalA += 1;
  //std::cout << "interval B (" << currentIntervalB << "): " << std::endl;
  vbyte::getNextInterval(&inB, previousB, startB, endB);
  currentIntervalB += 1;


  bool intervalRcontained = false;
  bool intersect = false;

  do {
    // printf("Intervals: [%u,%u) and [%u,%u)\n", startA, endA, startB, endB);
    if(startA >= startB && endA <= endB){
      intervalRcontained = true;
      intersect = true;
      
    }else if(startA<=startB && endA>startB){
      // there is an intersection
      intersect = true;
    }else if(startA>startB && startA<endB){
      // there is an intersection
      intersect = true;
    }
    if (endA<=endB)
    {
      if(!intervalRcontained){
        // found an interval that is not contained, continue looking for intersections
        // if one was not found yet
        if (!intersect) {
          // get next interval
          vbyte::getNextInterval(&inA, previousA, startA, endA);
          currentIntervalA += 1;
          // and jump to continue looking for intersection
          goto LOOK_FOR_OVERLAP;
        }
        // we have an intersection and non-containment, so return non-containment
        // return spatial_lib::IL_INTERSECT;
        return 1;
      }
      
      vbyte::getNextInterval(&inA, previousA, startA, endA);
      currentIntervalA += 1;
      intervalRcontained = false;
    }
    else {
      vbyte::getNextInterval(&inB, previousB, startB, endB);
      currentIntervalB += 1;
    }

  } while(currentIntervalA <= lengthA && currentIntervalB <= lengthB);

  //if we didnt check all of the R intervals
  if(currentIntervalA <= lengthA/* || !intervalRcontained*/){	
    // printf("returning non-containment\n");
    if(intersect) {
        // return spatial_lib::IL_INTERSECT;
        return 1;
    }
    // return spatial_lib::IL_DISJOINT;
    return 0;
  }
  //all intervals R were contained
  // return spatial_lib::IL_R_INSIDE_S;
  return 2;
  
LOOK_FOR_OVERLAP:
  // after this point it is guaranteed NO containment. So after 1 intersection is found, return
  do {
    if (startA<=startB)
    {
      if (endA>startB) // overlap, end1>=st2 if intervals are [s,e] and end1>st2 if intervals are [s,e)
      {
        //they intersect
        // return spatial_lib::IL_INTERSECT;
        return 1;
        break;
      }	
      else
      {
        vbyte::getNextInterval(&inA, previousA, startA, endA);
        currentIntervalA += 1;
      }
    }
    else // st2<st1
    {
      if (endB>startA) // overlap, end2>=st1 if intervals are [s,e] and end2>st1 if intervals are [s,e)
      {

        //they intersect
        // return spatial_lib::IL_INTERSECT;
        return 1;
        break;
      }
      else
      {
        vbyte::getNextInterval(&inB, previousB, startB, endB);
        currentIntervalB += 1;
      }
    }
  } while(currentIntervalA <= lengthA && currentIntervalB <= lengthB);

  // guaranteed non-containment and no intersection at this point
  // return spatial_lib::IL_DISJOINT;
  return 0;
}

int vbyte_inside_join_sorted23(const uint8_t *inA, size_t lengthA, const uint8_t *inB, size_t lengthB) {
  if (lengthA == 0 || lengthB == 0) {
    return 0;
  }
  //Completely ignored AVX method
  uint32_t startA, endA, startB, endB;

  size_t currentIntervalA = 0;
  size_t currentIntervalB = 0;

  uint32_t previousA = 0;
  uint32_t previousB = 0;
  //get first interval
  //std::cout << "interval A (" << currentIntervalA << "): " << std::endl;
  vbyte::getNextInterval(&inA, previousA, startA, endA);
  currentIntervalA += 1;
  //std::cout << "interval B (" << currentIntervalB << "): " << std::endl;
  vbyte::getNextInterval(&inB, previousB, startB, endB);
  currentIntervalB += 1;

  bool intervalRcontained = false;

  // printf("Begining...\n");
  do {
    // printf("Intervals: [%u,%u) and [%u,%u)\n", startA, endA, startB, endB);
    if(startA >= startB && endA <= endB){
      intervalRcontained = true;
    }
    if (endA<=endB)
    {
      if(!intervalRcontained){
        //we are skipping this interval because it was never contained, so return false (not inside)
        return 0;
      }
      vbyte::getNextInterval(&inA, previousA, startA, endA);
      currentIntervalA += 1;
      intervalRcontained = false;
    }
    else 
    {
      vbyte::getNextInterval(&inB, previousB, startB, endB);
      currentIntervalB += 1;
    }

  } while(currentIntervalA <= lengthA && currentIntervalB <= lengthB);

  //if we didnt check all of the R intervals
  if(currentIntervalA <= lengthA){	
    return 0;
  }
  //all intervals R were contained
  return 1;

}

int vbyte_join_compressed_sorted32_symmetrical(const uint8_t *inA, size_t lengthA, const uint8_t *inB, size_t lengthB){
  if (lengthA == 0 || lengthB == 0) {
    return 0;
  }
  //Completely ignored AVX method
  uint32_t startA, endA, startB, endB;

  bool RinS = false;
  bool SinR = false;

  // is intervals R contained in intervals S?
  if (vbyte_inside_join_sorted23(inA, lengthA, inB, lengthB)) {
    RinS = true;
  }
  // is intervals S contained in intervals R?
  if (vbyte_inside_join_sorted23(inB, lengthB, inA, lengthA)) {
    SinR = true;
  }
  printf("RinS: %d, SinR: %d\n", RinS, SinR);
  if (RinS && SinR) {
    // if both are contained, they match
    // return spatial_lib::IL_MATCH;
    return 4;
  } else if(RinS) {
    // return spatial_lib::IL_R_INSIDE_S;
    return 2;
  } else if(SinR) {
    // return spatial_lib::IL_S_INSIDE_R;
    return 3;
  } else {
    // return spatial_lib::IL_INTERSECT;
    return 1;
  }


}



/* 32 bit */
void vbyte_print_intervals(const uint8_t *in, size_t length){
  
  //Completely ignored AVX method
  uint32_t startA, endA;

  size_t currentIntervalA = 0;

  uint32_t previousA = 0;

  //std::cout << lengthA << " intervals in A" << std::endl;  

  //get first interval
  //std::cout << "interval A (" << currentIntervalA << "): " << std::endl;
  vbyte::getNextInterval(&in, previousA, startA, endA);
  currentIntervalA += 1;

  do {
    std::cout << "[" << startA << "," << endA << ")" << std::endl;
    //std::cout << "interval A (" << currentIntervalA << "): " << std::endl;
    vbyte::getNextInterval(&in, previousA, startA, endA);
    currentIntervalA += 1;
    
  } while(currentIntervalA <= length);
}

/* different granularities */
// int vbyte_join_compressed_different_granularities(const uint8_t *inA, size_t lengthA, const uint8_t *inB, size_t lengthB, uint &powerA, uint &powerB){
  
//   //Completely ignored AVX method
//   uint32_t startA, endA, startB, endB;
//   // uint32_t temp;

//   size_t currentIntervalA = 0;
//   size_t currentIntervalB = 0;

//   uint32_t previousA = 0;
//   uint32_t previousB = 0;



//   // std::cout << "A = " << powerA << " | B = " << powerB << std::endl;

//   if(powerA < powerB){
//     //A lower granularity than B so: scale B down
//     int granularityDiff = powerB - powerA;
//     int bitsToShift = granularityDiff*2;

//     // std::cout << "A = " << powerA << " < B = " << powerB << std::endl;

//     //std::cout << lengthA << " intervals in A" << std::endl;  
//     //std::cout << lengthB << " intervals in B" << std::endl;

//     //get first interval
//     //std::cout << "interval A (" << currentIntervalA << "): " << std::endl;
//     vbyte::getNextInterval(&inA, previousA, startA, endA);
//     currentIntervalA += 1;
//     //std::cout << "interval B (" << currentIntervalB << "): " << std::endl;
//     vbyte::getNextInterval(&inB, previousB, startB, endB);
//     currentIntervalB += 1;

//     //shift
//     // std::cout << "   shifted [" << startB << "," << endB << ") to [";
//     startB = startB >> bitsToShift;
//     // temp = endB;
//     // endB = (endB >> bitsToShift);
//     // if(startB == endB)
//     //   endB += 1;
//     // else if((temp-1) >> bitsToShift == endB){
//     //   endB += 1;
//     // }
//     endB = (endB-1) >> bitsToShift;

//     // std::cout << startB << "," << endB << ")" << std::endl;

//     // std::cout << "   shifted [" << startA << "," << endA << ") to [";
//     // startA = startA << bitsToShift;
//     // endA = endA << bitsToShift;
//     // std::cout << startA << "," << endA << ")" << std::endl;

//     do {
//       // std::cout << "[" << startA << "," << endA << ") with [" << startB << "," << endB << ")" << std::endl;
//       if (startA<=startB)
//       {
//         if (endA-1>=startB) // overlap, endA>=startB if intervals are [s,e] and endA>startB if intervals are [s,e)
//         {
//           //they overlap, return 1
//           // std::cout << "Overlap: " << startA << "," << endA << " and " << startB << "," << endB << std::endl;
//           // std::cout << "  checked " << currentIntervalA << " and " << currentIntervalB << " intervals and found a HIT." << std::endl;
//           return 1;
//         } 
//         else
//         {
//           //std::cout << "interval A (" << currentIntervalA << "): " << std::endl;
//           vbyte::getNextInterval(&inA, previousA, startA, endA);
//           currentIntervalA += 1;
          
//           // std::cout << "   shifted [" << startA << "," << endA << ") to [";
//           // startA = startA << bitsToShift;
//           // endA = endA << bitsToShift;
//           // std::cout << startA << "," << endA << ")" << std::endl;
//         }
//       }
//       else // startB<startA
//       {
//         if (endB>=startA) // overlap, endB>=startA if intervals are [s,e] and endB>startA if intervals are [s,e)
//         {

//           //they overlap, return 1
//           // std::cout << "Overlap: " << startA << "," << endA << " and " << startB << "," << endB << std::endl;
//           // std::cout << "  checked " << currentIntervalA << " and " << currentIntervalB << " intervals and found a HIT." << std::endl;
//           return 1;
//         }
//         else
//         {
//           //std::cout << "interval B (" << currentIntervalB << "): " << std::endl;
//           vbyte::getNextInterval(&inB, previousB, startB, endB);
//           currentIntervalB += 1;
//           // std::cout << "   shifted [" << startB << "," << endB << ") to [";
//           startB = startB >> bitsToShift;
//           // temp = endB;
//           // endB = (endB >> bitsToShift);
//           // if(startB == endB)
//           //   endB += 1;
//           // else if((temp-1) >> bitsToShift == endB){
//           //   endB += 1;
//           // }
//           endB = (endB-1) >> bitsToShift;
//           // std::cout << startB << "," << endB << ")" << std::endl;

//           // std::cout << "   shifted [" << startA << "," << endA << ") to [";
          
//           // std::cout << startA << "," << endA << ")" << std::endl;

//           // while(startB == endB){
//           //   std::cout << "skipped [" << startB << "," << endB << ")" << std::endl;

//           //   vbyte::getNextInterval(&inB, previousB, startB, endB);
//           //   currentIntervalB += 1;
//           //   startB = startB >> bitsToShift;
//           //   endB = endB >> bitsToShift;
//           // }
//         }
//       }


//     } while(currentIntervalA <= lengthA && currentIntervalB <= lengthB);

//     // std::cout << "  checked " << currentIntervalA-1 << " and " << currentIntervalB-1 << " intervals and DIDNT find a hit." << std::endl;
//     return 0;


//   }else{    
//     //B lower granularity than A so: scale A down

//     int granularityDiff = powerA - powerB;
//     int bitsToShift = granularityDiff*2;


//     //std::cout << lengthA << " intervals in A" << std::endl;  
//     //std::cout << lengthB << " intervals in B" << std::endl;

//     //get first interval
//     //std::cout << "interval A (" << currentIntervalA << "): " << std::endl;
//     vbyte::getNextInterval(&inA, previousA, startA, endA);
//     currentIntervalA += 1;
//     //std::cout << "interval B (" << currentIntervalB << "): " << std::endl;
//     vbyte::getNextInterval(&inB, previousB, startB, endB);
//     currentIntervalB += 1;

//     //shift
//     // std::cout << "shifted [" << startA << "," << endA << ") to ["; 
//     startA = startA >> bitsToShift;
//     // temp = endA;
//     // endA = (endA >> bitsToShift);    
//     // if(startA == endA){
//     //   // std::cout << "changed endA from " << endA << " to " << endA + 1 << std::endl;
//     //   endA += 1;
//     // }else if((temp-1) >> bitsToShift == endA){
//     //   endA += 1;
//     // }
//     endA = (endA - 1) >> bitsToShift;
//     // std::cout << startA << "," << endA << ")" << std::endl;

//     do {
//       // std::cout << "[" << startA << "," << endA << ") with [" << startB << "," << endB << ")" << std::endl;
//       if (startA<=startB)
//       {
//         if (endA>=startB) // overlap, endA>=startB if intervals are [s,e] and endA>startB if intervals are [s,e)
//         {
//           //they overlap, return 1
//           // std::cout << "Overlap: " << startA << "," << endA << " and " << startB << "," << endB << std::endl;
//           // std::cout << "  checked " << currentIntervalA << " and " << currentIntervalB << " intervals and found a HIT." << std::endl;
//           return 1;
//         } 
//         else
//         {
//           //std::cout << "interval A (" << currentIntervalA << "): " << std::endl;
//           vbyte::getNextInterval(&inA, previousA, startA, endA);
//           currentIntervalA += 1;
//           //shift
//           // std::cout << "shifted [" << startA << "," << endA << ") to ["; 
//           startA = startA >> bitsToShift;
//           // temp = endA;
//           // endA = (endA >> bitsToShift);
//           // if(startA == endA){
//           //   // std::cout << "changed endA from " << endA << " to " << endA + 1 << std::endl;
//           //   endA += 1;
//           // }else if((temp-1) >> bitsToShift == endA){
//           //   endA += 1;
//           // }
//           endA = (endA - 1) >> bitsToShift;
//           // std::cout << startA << "," << endA << ")" << std::endl;

//         }
//       }
//       else // startB<startA
//       {
//         if (endB-1>=startA) // overlap, endB>=startA if intervals are [s,e] and endB>startA if intervals are [s,e)
//         {

//           //they overlap, return 1
//           // std::cout << "Overlap: " << startA << "," << endA << " and " << startB << "," << endB << std::endl;
//           // std::cout << "  checked " << currentIntervalA << " and " << currentIntervalB << " intervals and found a HIT." << std::endl;
//           return 1;
//         }
//         else
//         {
//           //std::cout << "interval B (" << currentIntervalB << "): " << std::endl;
//           vbyte::getNextInterval(&inB, previousB, startB, endB);
//           currentIntervalB += 1;          
//         }
//       }


//     } while(currentIntervalA <= lengthA && currentIntervalB <= lengthB);

//     // std::cout << "  checked " << currentIntervalA-1 << " and " << currentIntervalB-1 << " intervals and DIDNT find a hit." << std::endl;
//     return 0;
  
//   }
// }
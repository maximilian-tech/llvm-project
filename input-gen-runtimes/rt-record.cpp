/*
Use via

clang++  -std=c++20 -O2 -mllvm --include-input-gen -mllvm -input-gen-mode=record
test_simple.c
/scr/maximilian.sander/repos/llvm-project/input-gen-runtimes/rt-record.cpp &&
./a.out

*/

/**
 * This implementation should do the following
 * 1. Store Argumente Values and Pointers ( __record_arg_ptr/... )?
 * 2. Store Memory accesses ( __record_access_i32/... )?
 *
 *
 */

/* ------------------------------------------------------------------------------
 */

/**
 * I'd like to access memory which is stored? At another location and want to
 * make it accessible eventually via MPI/ZeroMQ The memory should either be
 * copied to another place, or be available as long as the memory is valid
 * (atomic operation?).
 *
 *
 */

/* ------------------------------------------------------------------------------
 */
#include <algorithm>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <format>
#include <fstream>
#include <iostream>
#include <type_traits>
#include <utility>
#include <vector>

#include "../llvm/include/llvm/Transforms/IPO/InputGenerationTypes.h"

using VoidPtrTy = uint8_t *;

class BranchHint {};

// Concept to ensure MemoryHandler has read and write methods
template <typename T>
concept MemoryHandlerConcept = requires(T t, VoidPtrTy ptr, int32_t size) {
  { t.template read<int>(ptr, size) } -> std::same_as<int>;
  { t.template write<int>(ptr, 0, size) };
  { t.dump() };
};

// MemorySegment class template
template <typename T> class MemorySegment {
public:
  MemorySegment(VoidPtrTy startPtr, std::size_t size)
      : m_start(startPtr), m_end(startPtr + size) {}

  bool isAdjacentTo(const MemorySegment &other) const {
    return this->m_end == other.m_start || this->m_start == other.m_end;
  }

  void merge(const MemorySegment &other) {
    if (this->m_end == other.m_start) {
      this->m_end = other.m_end;
    } else if (this->m_start == other.m_end) {
      this->m_start = other.m_start;
    }
  }

  void fillData() {
    std::size_t size = m_end - m_start;
    data.resize(size);
    std::memcpy(data.data(), m_start, size);
  }

  const std::vector<T> &getData(void) { return data; }

  const VoidPtrTy start() const { return m_start; }
  const VoidPtrTy end() const { return m_end; }

private:
  VoidPtrTy m_start;
  VoidPtrTy m_end;
  std::vector<T> data;
};

template <typename T>
void write_to_disk(const std::string &fileName, MemorySegment<T> &segment) {
  std::ofstream outFile(fileName);

  for (const auto content : segment.getData()) {
    outFile << content;
  }
  outFile.close();
}

// MemorySegmentHandler class
class MemorySegmentHandler {
public:
  template <typename TY> void write(VoidPtrTy Ptr, TY Val, int32_t Size) {
    std::memcpy(Ptr, &Val, Size);
    addElement(Ptr, Size);
  }

  template <typename TY> TY read(VoidPtrTy Ptr, int32_t Size) {
    TY Val;
    std::memcpy(&Val, Ptr, Size);
    return Val;
  }

  void addElement(VoidPtrTy Ptr, std::size_t Size) {
    MemorySegment<uint8_t> newSegment(Ptr, Size);

    for (auto &segment : m_segments) {
      if (segment.isAdjacentTo(newSegment)) {
        segment.merge(newSegment);
        mergeLocalSegments();
        std::cout << " MERGED! " << std::endl;
        return;
      }
    }

    m_segments.push_back(newSegment);
    mergeLocalSegments();
  }

  void mergeLocalSegments() {
    for (std::size_t i = m_segments.size(); i-- > 0;) {
      for (std::size_t j = m_segments.size(); j-- > i + 1;) {
        if (m_segments[i].isAdjacentTo(m_segments[j])) {
          m_segments[i].merge(m_segments[j]);
          m_segments.erase(m_segments.begin() + j);
        }
      }
    }
  }

  void fillAllSegmentsData() {
    for (auto &segment : m_segments) {
      segment.fillData();
      std::cout << " FILL! " << std::endl;
    }
  }

  void dump() {
    fillAllSegmentsData();

    int i = 0;
    for (auto &segment : m_segments) {
      write_to_disk(std::format("output_{}.txt", i++), segment);
    }
  }

private:
  std::vector<MemorySegment<uint8_t>> m_segments;
};

// AccessHandler class
template <MemoryHandlerConcept MemoryHandler> class AccessHandler {
public:
  explicit AccessHandler(MemoryHandler *memory) : m_memory(memory) {}

  template <typename TY> void read(VoidPtrTy Ptr, int32_t Size) {
    m_memory->template read<TY>(Ptr, Size);
    std::cout << " READ! " << std::endl;
  }

  template <typename TY> void write(VoidPtrTy Ptr, TY Val, int32_t Size) {
    m_memory->template write<TY>(Ptr, Val, Size);
    std::cout << " WRITE! " << std::endl;
  }

  void dump(void) { m_memory->dump(); }

private:
  MemoryHandler *m_memory;
};

AccessHandler<MemorySegmentHandler> &getAccessHandler() {
  static MemorySegmentHandler memoryHandler;
  static AccessHandler<MemorySegmentHandler> instance(&memoryHandler);
  return instance;
}

template <typename TY>
static void access(VoidPtrTy Ptr, int64_t Val, int32_t Size, int32_t Kind) {
  switch (Kind) {
  case 0:
    getAccessHandler().template read<TY>(Ptr, Size);
    return;
  case 1: {
    TY TyVal = [Val]() {
      if constexpr (std::is_same_v<TY, float>) {
        int32_t Trunc = static_cast<int32_t>(Val);
        float result;
        std::memcpy(&result, &Trunc, sizeof(result));
        return result;
      } else if constexpr (std::is_same_v<TY, double>) {
        double result;
        std::memcpy(&result, &Val, sizeof(result));
        return result;
      } else if constexpr (std::is_pointer_v<TY>) {
        return reinterpret_cast<TY>(static_cast<uintptr_t>(Val));
      } else {
        return static_cast<TY>(Val);
      }
    }();
    getAccessHandler().template write<TY>(Ptr, TyVal, Size);
    return;
  }
  default:
    std::abort();
  }
}

extern "C" {

#define RW(TY, NAME)                                                           \
  __attribute__((always_inline)) void __record_access_##NAME(                  \
      VoidPtrTy Ptr, int64_t Val, int32_t Size, VoidPtrTy Base, int32_t Kind,  \
      BranchHint *BHs, int32_t BHSize) {                                       \
    access<TY>(Ptr, Val, Size, Kind);                                          \
  }

RW(bool, i1)
RW(char, i8)
RW(short, i16)
RW(int32_t, i32)
RW(int64_t, i64)
RW(float, float)
RW(double, double)
RW(VoidPtrTy, ptr)
#undef RW

#define ARG(TY, NAME)                                                          \
  __attribute__((always_inline)) TY __record_arg_##NAME(BranchHint *BHs,       \
                                                        int32_t BHSize) {}

ARG(bool, i1)
ARG(char, i8)
ARG(short, i16)
ARG(int32_t, i32)
ARG(int64_t, i64)
ARG(float, float)
ARG(double, double)
ARG(VoidPtrTy, ptr)
#undef ARG

void __record_push(void) {}
void __record_pop(void) { getAccessHandler().dump(); }
}

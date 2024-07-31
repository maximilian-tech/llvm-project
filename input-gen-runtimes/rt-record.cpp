/*
Use via

clang++  -std=c++20 -O2 -mllvm --include-input-gen -mllvm -input-gen-mode=record
test_simple.c -input-gen-entry-point=_Z3addPiS_S_i
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

template <typename TY> struct MemoryOperationArgsWrite {
  VoidPtrTy Ptr;
  TY Val;
  int32_t Size;
  std::string Name;
};

// Concept to ensure MemoryHandler has read and write methods
template <typename T>
concept MemoryHandlerConcept = requires(T t, VoidPtrTy ptr, int32_t size,
                                        MemoryOperationArgsWrite<T> args) {
  { t.template read<T>(ptr, size) } -> std::same_as<T>;
  { t.template write(args) };
  { t.dump() };
};

// MemorySegment class template
template <typename T> class MemorySegment {
public:
  MemorySegment(VoidPtrTy startPtr, std::size_t size, const std::string &name)
      : m_start(startPtr), m_end(startPtr + size), m_name(name) {}

  bool isAdjacentTo(const MemorySegment &other) const {
    return (this->m_end == other.m_start || this->m_start == other.m_end) &&
           this->m_name == other.m_name;
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

  const std::vector<T> &getData() const { return data; }

  const VoidPtrTy start() const { return m_start; }
  const VoidPtrTy end() const { return m_end; }
  const std::string &getName() const { return m_name; }

private:
  VoidPtrTy m_start;
  VoidPtrTy m_end;
  std::string m_name;
  std::vector<T> data;
};

template <typename T>
void write_to_disk(const std::string &fileName,
                   const MemorySegment<T> &segment) {
  std::ofstream outFile(fileName);

  outFile << segment.getName() << "\n";

  for (const auto &content : segment.getData()) {
    outFile << content;
  }
  outFile.close();
}

// MemorySegmentHandler class
class MemorySegmentHandler {
public:
  template <typename TY> void write(const MemoryOperationArgsWrite<TY> &args) {
    std::memcpy(args.Ptr, &args.Val, args.Size);
    addElement(args);
  }

  template <typename TY> TY read(VoidPtrTy Ptr, int32_t Size) {
    TY Val;
    std::memcpy(&Val, Ptr, Size);
    return Val;
  }

  template <typename TY>
  void addElement(const MemoryOperationArgsWrite<TY> &args) {
    MemorySegment<uint8_t> newSegment(args.Ptr, args.Size, args.Name);

    for (auto &segment : m_segments) {
      if (segment.isAdjacentTo(newSegment)) {
        segment.merge(newSegment);
        std::cout << "MERGED_ADJ!" << std::endl;
        mergeLocalSegments();
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
          std::cout << "MERGED_LOC!" << std::endl;
        }
      }
    }
  }

  void fillAllSegmentsData() {
    for (auto &segment : m_segments) {
      segment.fillData();
      std::cout << "FILL!" << std::endl;
    }
  }

  void dump() {
    fillAllSegmentsData();

    int i = 0;
    for (const auto &segment : m_segments) {
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
    std::cout << "READ!" << std::endl;
  }

  template <typename TY> void write(const MemoryOperationArgsWrite<TY> &args) {
    m_memory->template write<TY>(args);
    std::cout << "WRITE!" << std::endl;
  }

  void dump() { m_memory->dump(); }

private:
  MemoryHandler *m_memory;
};

AccessHandler<MemorySegmentHandler> &getAccessHandler() {
  static MemorySegmentHandler memoryHandler;
  static AccessHandler<MemorySegmentHandler> instance(&memoryHandler);
  return instance;
}

template <typename TY>
static void access(VoidPtrTy Ptr, int64_t Val, int32_t Size, int32_t Kind,
                   char *Name) {
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
    getAccessHandler().template write<TY>(
        MemoryOperationArgsWrite<TY>{Ptr, TyVal, Size, Name});
    return;
  }
  default:
    std::abort();
  }
}

extern "C" {

#define RW(TY, NAME)                                                           \
  __attribute__((always_inline)) void __record_access_##NAME(                  \
      VoidPtrTy Ptr, int64_t Val, int32_t Size, VoidPtrTy /*Base*/,            \
      int32_t Kind, BranchHint * /*BHs*/, int32_t /*BHSize*/, char *Name) {    \
    access<TY>(Ptr, Val, Size, Kind, Name);                                    \
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
  __attribute__((always_inline)) TY __record_arg_##NAME(BranchHint * /*BHs*/,  \
                                                        int32_t /*BHSize*/) {}

ARG(bool, i1)
ARG(char, i8)
ARG(short, i16)
ARG(int32_t, i32)
ARG(int64_t, i64)
ARG(float, float)
ARG(double, double)
ARG(VoidPtrTy, ptr)
#undef ARG

void __record_cmp_ptr(VoidPtrTy A, VoidPtrTy B, int32_t Predicate) {
  // getInputGenRT().cmpPtr(A, B, Predicate);
}

void __record_unreachable(int32_t No, const char *Name) {
  // printf("Reached unreachable %i due to '%s'\n", No, Name ? Name : "n/a");
  // exit(UnreachableExitStatus);
}

void __record_push(void) {}
void __record_pop(void) { getAccessHandler().dump(); }
}

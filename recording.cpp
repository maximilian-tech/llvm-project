#include <bitset>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <dlfcn.h>
#include <map>
#include <stdio.h>
#include <type_traits>
#include <vector>

template <typename Ty = void *> static Ty advance(void *Ptr, uint64_t Bytes) {
  return reinterpret_cast<Ty>(reinterpret_cast<char *>(Ptr) + Bytes);
}

extern "C" {

static std::map<void *, size_t> *ObjMap;
static int32_t RecordingLevel = 0;
static void *Buffer, *InitialBuffer;
static const size_t BufferSize = 6L * 1024 * 1024 * 1024;           // 6 GB
static const size_t TrackingOffset = BufferSize / 3;                // 2 GB
static const size_t ShadowOffset = TrackingOffset + BufferSize / 3; // 4 GB
static const size_t UserSize = TrackingOffset;                      // 2 GB

static void *(*real_malloc)(size_t) = NULL;
static void (*real_free)(void *) = NULL;

static void mtrace_init(void) {
  real_malloc = (decltype(real_malloc))dlsym(RTLD_NEXT, "malloc");
  real_free = (decltype(real_free))dlsym(RTLD_NEXT, "free");
  if (!real_malloc || !real_free)
    exit(2);
  InitialBuffer = Buffer = real_malloc(BufferSize);                // Alloc 6 GB
  memset(advance(Buffer, UserSize), 0, BufferSize - UserSize);      // memset 4 GB to '0', starting by (Buffer+2GB)
  ++RecordingLevel;
  ObjMap = new std::map<void *, size_t>;
  --RecordingLevel;
}

void *malloc(size_t Size) {
  if (!real_malloc)
    mtrace_init();

  void *P = NULL;
  if (RecordingLevel) { // RecordingLevel != 0
    P = real_malloc(Size);
  } else {              // RecordingLevel == 0
    P = Buffer;         // return current buffer location
    Size += Size % 16;  // Align 16 Bytes
    Buffer = advance(Buffer, Size); // Advance Buffer
    ++RecordingLevel;    // Increase Recording Level to prevent accidental malloc tracing
    (*ObjMap)[P] = Size; // Store Pair < Pointer | Size > within ObjMap
    --RecordingLevel;
  }
  // fprintf(stderr, "malloc(%zu): %p [%i]\n", Size, P, RecordingLevel);
  return P;
}
void free(void *P) {
  // fprintf(stderr, "free(%p)\n", P);
  auto It = ObjMap->find(P);    // Find the Pointer previously stored
  if (It == ObjMap->end()) {    // If not found, 'simplpy' free
    real_free(P);
    return;
  }
  if (Buffer == advance(P, It->second)) // If Bufferposition == PointerToFree+PointerToFreeSize (last element.)
  {
    Buffer = advance(Buffer, -It->second);      // --> reduce buffer size        ( ## ToDo: How effective is this?)
  }
  ObjMap->erase(It);                          // Remove Pair
}

void __record_version_mismatch_check_v1() {}

void __record_init() {
  printf("Init: %p : %p : %p : %p\n", InitialBuffer, // Print Adresses
         advance(InitialBuffer, TrackingOffset),
         advance(InitialBuffer, ShadowOffset),
         advance(InitialBuffer, BufferSize));
  srand(time(NULL));
}

void __record_deinit() {
  printf("Deinit: %zu\n", ObjMap->size()); // Prints number of elements (e.g. persisting pointers (e.g. unfreed))
  for (auto &It : *ObjMap) {
    printf("%p : %p [%zu]\n", It.first, advance(It.first, It.second), // Prints Memory Regions: start, stop, [size]
           It.second);
  }
  printf("Used %lu bytes\n", (char *)Buffer - (char *)InitialBuffer);   // Data stored in Buffer (unfreed data )
  uint32_t *TPtr = advance<uint32_t *>(InitialBuffer, TrackingOffset);  // Pointer 'TrackingOffset' into the InitialBuffer
  for (size_t I = 0; I < TrackingOffset; I += sizeof(*TPtr)) {          // Loop through tracking offset
    if (*advance<uint32_t *>(TPtr, I) != 17)                            // If not 17 ( ?? ) continue --> only "17" fields are interesting
      continue;
    void *P = advance(TPtr, I - TrackingOffset);                        // Give Pointer "under" the tracking offset (which now guids as an upper barrier?) (Prob. due to alignement.)
    auto It = ObjMap->lower_bound(P);               // Iterator pointing to P ???? Why Shoudl that work?
    if (It == ObjMap->end() || It->first > P) {     // If points to last element, or Iterator starts behind, subtract '1' It.
      --It;
    }
    if (It->first <= P && advance(It->first, It->second) > P)   // If First Iterator Element points at or before P, and the pointer has size > 0 (effektively)
    {
      printf("Got %p at %zu: Obj(%zu): [", P, I, It->second);
      uint32_t *ItTPtr = advance<uint32_t *>(It->first, TrackingOffset);
      uint32_t *ItSPtr = advance<uint32_t *>(It->first, ShadowOffset);
      //      for (int J = 0; J < It->second; J += sizeof(*TPtr))
      //        if (*advance<uint32_t *>(ItTPtr, J) == 17)
      //          printf("%d", *advance<uint32_t *>(ItSPtr, J));
      //        else
      //          printf("-");
      printf("]\n");
      I += It->second - sizeof(*TPtr);              // Manually Index 'I' after the allocated size of the pointer.
      continue;
    }
    printf("Got %p at %zu: Unknown\n", P, I);
  }
}

void __record_push() { ++RecordingLevel; }

void __record_pop() { --RecordingLevel; }

// Basically the following two subroutines only work, if our pointer

void mark(void *Ptr, int32_t Size) {
  if (Ptr < InitialBuffer || Ptr >= advance(InitialBuffer, UserSize))  // Out of Bounds Checker. Here we check whether the pointer is within a range we assigned it to (if so)
    return;
  uint32_t *TPtr = advance<uint32_t *>(Ptr, TrackingOffset);           // Give pointer '2G' into Ptr. This is 'allocated' by our own buffer.
  for (int I = 0; I < Size; I += sizeof(*TPtr)) {                      // Iterate over array size
    if (!TPtr[I]) {                                                    // If not already marked, mark
        *advance<uint32_t *>(TPtr, I) = 1;
    }
  }
}

void checkAndRemember(void *Ptr, int32_t Size) {
  if (Ptr < InitialBuffer || Ptr >= advance(InitialBuffer, UserSize)) // Out of Bounds Checker (We effectively raw index into some memory hoping we recvh it.?)
    return;
  uint32_t *TPtr = advance<uint32_t *>(Ptr, TrackingOffset);
  uint32_t *SPtr = advance<uint32_t *>(Ptr, ShadowOffset);
  for (int I = 0; I < Size; I += sizeof(*TPtr)) {
    if (TPtr[I])                        // if written to or read -> skip
      continue;
    *advance<uint32_t *>(TPtr, I) = 17; // Mark cell as visited ( Read )
    *advance<uint32_t *>(SPtr, I) = *advance<uint32_t *>(Ptr, I); // Copy read value into own buffer
  }
}

#define RW(TY, NAME)                                                           \
  __attribute__((always_inline)) void __record_read_##NAME(                    \
      void *Ptr, int64_t Val, int32_t Size, void *Base) {                      \
    checkAndRemember(Ptr, Size);                                               \
  }                                                                            \
  __attribute__((always_inline)) void __record_write_##NAME(                   \
      void *Ptr, int64_t Val, int32_t Size, void *Base) {                      \
    mark(Ptr, Size);                                                           \
  }

RW(bool, i1)
RW(char, i8)
RW(short, i16)
RW(int32_t, i32)
RW(int64_t, i64)
RW(float, float)
RW(double, double)
RW(void *, ptr)
#undef RW

#define ARG(TY, NAME)                                                          \
  TY __record_arg_##NAME(TY Arg) { return Arg; }

ARG(bool, i1)
ARG(char, i8)
ARG(short, 16)
ARG(int32_t, i32)
ARG(int64_t, i64)
ARG(float, float)
ARG(double, double)
ARG(void *, ptr)
#undef ARG
}

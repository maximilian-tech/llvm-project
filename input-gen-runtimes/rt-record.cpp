/*
/usr/bin/ld: /tmp/test_simple-d4cea7.o: in function `__inputgen_renamed_add':
test_simple.c:(.text+0x0): multiple definition of `__inputgen_renamed_add'; /tmp/test_simple-9f02ef.o:test_simple.c:(.text+0x0): first defined here
/usr/bin/ld: /tmp/test_simple-d4cea7.o: in function `__inputgen_renamed_main':
test_simple.c:(.text+0xf0): multiple definition of `__inputgen_renamed_main'; /tmp/test_simple-9f02ef.o:test_simple.c:(.text+0xf0): first defined here
/usr/bin/ld: /tmp/test_simple-d4cea7.o:(.data.rel.ro+0x0): multiple definition of `__record_function_pointers'; /tmp/test_simple-9f02ef.o:(.data.rel.ro+0x0): first defined here
/usr/bin/ld: /tmp/test_simple-d4cea7.o:(.rodata+0x0): multiple definition of `__record_num_function_pointers'; /tmp/test_simple-9f02ef.o:(.rodata+0x0): first defined here
/usr/bin/ld: /usr/lib/gcc/x86_64-redhat-linux/13/../../../../lib64/Scrt1.o: in function `_start':
(.text+0x1b): undefined reference to `main'
/usr/bin/ld: /tmp/test_simple-9f02ef.o: in function `__inputgen_renamed_add':
test_simple.c:(.text+0x17): undefined reference to `__record_push'
/usr/bin/ld: test_simple.c:(.text+0x21): undefined reference to `__record_arg_ptr'
/usr/bin/ld: test_simple.c:(.text+0x2b): undefined reference to `__record_arg_ptr'
/usr/bin/ld: test_simple.c:(.text+0x35): undefined reference to `__record_arg_ptr'
/usr/bin/ld: test_simple.c:(.text+0x3c): undefined reference to `__record_arg_i32'
/usr/bin/ld: test_simple.c:(.text+0x7c): undefined reference to `__record_access_i32'
/usr/bin/ld: test_simple.c:(.text+0xa0): undefined reference to `__record_access_i32'
/usr/bin/ld: test_simple.c:(.text+0xc8): undefined reference to `__record_access_i32'
/usr/bin/ld: test_simple.c:(.text+0xe8): undefined reference to `__record_pop'
/usr/bin/ld: /tmp/test_simple-9f02ef.o: in function `__inputgen_renamed_main':
test_simple.c:(.text+0x166): undefined reference to `__record_access_i32'
/usr/bin/ld: test_simple.c:(.text+0x1aa): undefined reference to `__record_access_i32'
/usr/bin/ld: test_simple.c:(.text+0x1ee): undefined reference to `__record_access_i32'
/usr/bin/ld: /tmp/test_simple-d4cea7.o: in function `__inputgen_renamed_add':
test_simple.c:(.text+0x17): undefined reference to `__record_push'
/usr/bin/ld: test_simple.c:(.text+0x21): undefined reference to `__record_arg_ptr'
/usr/bin/ld: test_simple.c:(.text+0x2b): undefined reference to `__record_arg_ptr'
/usr/bin/ld: test_simple.c:(.text+0x35): undefined reference to `__record_arg_ptr'
/usr/bin/ld: test_simple.c:(.text+0x3c): undefined reference to `__record_arg_i32'
/usr/bin/ld: test_simple.c:(.text+0x7c): undefined reference to `__record_access_i32'
/usr/bin/ld: test_simple.c:(.text+0xa0): undefined reference to `__record_access_i32'
/usr/bin/ld: test_simple.c:(.text+0xc8): undefined reference to `__record_access_i32'
/usr/bin/ld: test_simple.c:(.text+0xe8): undefined reference to `__record_pop'
/usr/bin/ld: /tmp/test_simple-d4cea7.o: in function `__inputgen_renamed_main':
test_simple.c:(.text+0x166): undefined reference to `__record_access_i32'
/usr/bin/ld: test_simple.c:(.text+0x1aa): undefined reference to `__record_access_i32'
/usr/bin/ld: test_simple.c:(.text+0x1ee): undefined reference to `__record_access_i32'*/

#include <vector>

/**
 * This implementation should do the following
 * 1. Store Argumente Values and Pointers ( __record_arg_ptr/... )?
 * 2. Store Memory accesses ( __record_access_i32/... )?
 *
 *
 */


/* ------------------------------------------------------------------------------ */

/**
 * I'd like to access memory which is stored? At another location and want to make it accessible eventually via MPI/ZeroMQ
 * The memory should either be copied to another place, or be available as long as the memory is valid (atomic operation?).
 *
 *
 */

/* ------------------------------------------------------------------------------ */

//#include <vector>
#include <algorithm>
#include <cstddef>
#include <cstdint>

#include "../llvm/include/llvm/Transforms/IPO/InputGenerationTypes.h"
// Using 'using' keyword to create an alias
using VoidPtrTy = uint8_t*;

// MemorySegment class template
template<typename T>
class MemorySegment : public std::vector<T> {
public:
    VoidPtrTy m_start;
    VoidPtrTy m_end; // May not work. Needs to point _after_ end??

    MemorySegment(VoidPtrTy startPtr, std::size_t size)
        : m_start(startPtr), m_end(startPtr + size) {
        this->resize(size);
        std::copy(startPtr, startPtr + size, this->begin());
    }

    // Check if another segment touches this segment
    bool touches(const MemorySegment& other) const {
        return this->m_end == other.m_start || this->m_start == other.m_end;
    }

    // Merge another segment into this segment
    void merge(const MemorySegment& other) {
        if (this->m_end == other.m_start) {
            this->insert(this->end(), other.begin(), other.end());
            this->m_end = other.m_end;
        } else if (this->m_start == other.m_end) {
            this->insert(this->begin(), other.begin(), other.end());
            this->m_start = other.m_start;
        }
    }
};
// MemorySegmentHandler class
class MemorySegmentHandler {
    std::vector<MemorySegment<uint8_t>> m_segments;

public:
    template <typename TY>
    void write(VoidPtrTy Ptr, TY Val, int32_t Size)
    {
        this->addElement(Ptr, Size);
    }
    template <typename TY>
    TY read(VoidPtrTy Ptr, int64_t Val, int32_t Size)
    {

    }

    void addElement(VoidPtrTy Ptr, std::size_t Size) {
        MemorySegment<uint8_t> newSegment(Ptr, Size);

        for (auto& segment : m_segments) {
            if (segment.touches(newSegment)) {
                segment.merge(newSegment);
                mergeLocalSegments();
                return;
            }
        }

        // No touching segment found, add as new segment
        m_segments.push_back(newSegment);
        mergeLocalSegments();
    }

    void mergeLocalSegments() {
        for (std::size_t i = m_segments.size(); i-- > 0;) {
            for (std::size_t j = m_segments.size(); j-- > i + 1;) {
                if (m_segments[i].touches(m_segments[j])) {
                    m_segments[i].merge(m_segments[j]);
                    m_segments.erase(m_segments.begin() + j);
                }
            }
        }
    }
};

template <typename MemoryHandler>
class AccessHandler
{
public:
    AccessHandler(MemoryHandler* memory): m_memory(memory) {}
       template<typename TY>
    TY getNewStub(BranchHint *BHs, int32_t BHSize) {
        // Example implementation
        return TY{};
    }

    template<typename TY>
    void read(VoidPtrTy Ptr, VoidPtrTy Base, int32_t Size, BranchHint *BHs, int32_t BHSize) {
        m_memory->read<TY>(Ptr, Base, Size);
    }

    template<typename TY>
    void write(VoidPtrTy Ptr, TY Val, int32_t Size) {
        m_memory->write<TY>(Ptr, Val, Size);
    }

private:
    MemoryHandler* m_memory;
};


AccessHandler& getAccessHandler() {
    static AccessHandler<MemorySegmentHandler> instance;
    return instance;
}


template <typename TY>
class RW {
public:
    static TY get(BranchHint *BHs, int32_t BHSize) {
        return getAccessHandler().getNewStub<TY>(BHs, BHSize);
    }

    static void access(VoidPtrTy Ptr, int64_t Val, int32_t Size, VoidPtrTy Base, int32_t Kind, BranchHint *BHs, int32_t BHSize) {
        switch (Kind) {
        case 0:
            getAccessHandler().read<TY>(Ptr, Base, Size, BHs, BHSize);
            return;
        case 1: {
            TY TyVal;
            if constexpr (std::is_same<TY, float>::value) {
                int32_t Trunc = static_cast<int32_t>(Val);
                TyVal = *reinterpret_cast<TY *>(&Trunc);
            } else if constexpr (std::is_same<TY, double>::value) {
                TyVal = *reinterpret_cast<TY *>(&Val);
            } else {
                TyVal = static_cast<TY>(Val);
            }
            getInputGenRT().write<TY>(Ptr, TyVal, Size);
            return;
        }
        default:
            std::abort();
        }
    }
};


#define DEFINE_RW(TY, NAME)                                                     \
  TY __inputgen_get_##NAME(BranchHint *BHs, int32_t BHSize) {                   \
    return RW<TY>::get(BHs, BHSize);                                            \
  }                                                                             \
  void __inputgen_access_##NAME(VoidPtrTy Ptr, int64_t Val, int32_t Size,       \
                                VoidPtrTy Base, int32_t Kind, BranchHint *BHs,  \
                                int32_t BHSize) {                               \
    RW<TY>::access(Ptr, Val, Size, Base, Kind, BHs, BHSize);                    \
  }

DEFINE_RW(bool, i1)
DEFINE_RW(char, i8)
DEFINE_RW(short, i16)
DEFINE_RW(int32_t, i32)
DEFINE_RW(int64_t, i64)
DEFINE_RW(float, float)
DEFINE_RW(double, double)
DEFINE_RW(VoidPtrTy, ptr)
#undef DEFINE_RW

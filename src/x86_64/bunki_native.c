#include "bunki.h"
#include "bunki_ctx.h"
#include <string.h>

void bunki_init_ctx(void);
extern uint32_t __bunki_patch0__;
extern uint32_t __bunki_patch1__;
extern uint32_t __bunki_patch2__;
extern uint32_t __bunki_patch3__;

#define ALIGN_MASK(val) (~((size_t)val - 1))

#if defined(_WIN32)
    #include <windows.h>
    #define  OBJ_RE    (PAGE_EXECUTE_READ)
    #define  OBJ_RWE   (PAGE_EXECUTE_READWRITE)

    static unsigned get_page_size(void) {
        #if defined(__x86_64__)
            return 4096;
        #else
            SYSTEM_INFO si;
            GetSystemInfo(&si);
            return si.dwPageSize;
        #endif
    }

    static unsigned patch_obj_mprotect(void* ptr, uint8_t obj_size, int prot) {
        uintptr_t pg_sz   = get_page_size();
        uintptr_t shift   = 0;
        uintptr_t ptr_beg = (uintptr_t)ptr;
        uintptr_t ptr_end = ptr_beg + obj_size - 1;
        ptr_beg &= -pg_sz;
        ptr_end &= -pg_sz;
        // straddles page boundary
        if(ptr_beg != ptr_end) {
            shift += 1;
        }
        DWORD old;
        return VirtualProtect((void*)ptr_beg, pg_sz << shift, prot, &old) == 0;
    }

#elif defined (__unix__) || (defined (__APPLE__) && defined (__MACH__))
    #include <unistd.h>
    #include <sys/mman.h>
    #define  OBJ_RE    (PROT_READ | PROT_EXEC)
    #define  OBJ_RWE   (PROT_READ | PROT_WRITE | PROT_EXEC)

    static unsigned get_page_size(void) {
        #if defined(__x86_64__)
            return 4096;
        #else
            long size = sysconf(_SC_PAGESIZE);
            return size;
        #endif
    }
    // Currently as written the object must only straddle two pages at most.
    static unsigned patch_obj_mprotect(void* ptr, uint8_t obj_size, int prot) {
        uintptr_t pg_sz   = get_page_size();
        uintptr_t shift   = 0;
        uintptr_t ptr_beg = (uintptr_t)ptr;
        uintptr_t ptr_end = ptr_beg + obj_size - 1;
        ptr_beg &= -pg_sz;
        ptr_end &= -pg_sz;
        // straddles page boundary
        if(ptr_beg != ptr_end) {
            shift += 1;
        }
        return mprotect((void*)ptr_beg, pg_sz << shift, prot) != 0;
    }

#endif

bunki_t bunki_native_finalize_ctx(bunki_t ctx, uintptr_t (*func)(void*), void* arg, uintptr_t stack_end) {
    uintptr_t stk = (uintptr_t)ctx;
    // If stack alignment does not match use a little space to fix that.
    stk &= ALIGN_MASK(ARCH_STK_ALIGN);
    struct stack_ctx_s* new_ctx = (struct stack_ctx_s*)(stk - sizeof(struct stack_ctx_s));
    // init_ctx moves arg where needed for calling argument then jumps to func.
    new_ctx->rip = (uintptr_t)bunki_init_ctx;
    new_ctx->rbx = (uintptr_t)func;
    new_ctx->r15 = (uintptr_t)arg;
    #if !defined(BUNKI_SHARE_FCW_MXCSR)
        #if defined(_WIN32)
            // https://docs.microsoft.com/en-us/cpp/build/x64-calling-convention?view=vs-2019#fpcsr
            new_ctx->fcw = 0x17F;
        #else
            // https://github.com/torvalds/linux/blob/63355b9884b3d1677de6bd1517cd2b8a9bf53978/arch/x86/kernel/fpu/core.c#L483
            new_ctx->fcw = 0x37f;
        #endif

        // https://www.amd.com/system/files/TechDocs/24592.pdf page 113 use default reset value
        // Bits 7-12 should be set
        // MS uses same reset value
        // https://docs.microsoft.com/en-us/cpp/build/x64-calling-convention?view=vs-2019#mxcsr
        new_ctx->mxcsr = 0x1F80;
    #endif
    #ifdef _WIN64
        new_ctx->stk_base    = stk;
        new_ctx->stk_limit   = stack_end;
        new_ctx->stk_dealloc = stack_end;
    #endif
    return new_ctx;
}

#if !defined(BUNKI_STACK_CONST)
unsigned bunki_patch_call_yield(uint32_t stack_size) {
    unsigned ret = 0;
    // Probably could do with less calls since I know some these patch points
    // are within a single page, but if I refactor any of the assembly at least
    // I do not need to update this.
    ret |= patch_obj_mprotect(&__bunki_patch0__, sizeof(uint32_t), OBJ_RWE);
    ret |= patch_obj_mprotect(&__bunki_patch1__, sizeof(uint32_t), OBJ_RWE);
    ret |= patch_obj_mprotect(&__bunki_patch2__, sizeof(uint32_t), OBJ_RWE);
    ret |= patch_obj_mprotect(&__bunki_patch3__, sizeof(uint32_t), OBJ_RWE);
    if(ret) {
        goto done;
    }
    stack_size -= 1;
    memcpy(&__bunki_patch0__, &stack_size, sizeof(uint32_t));
    memcpy(&__bunki_patch1__, &stack_size, sizeof(uint32_t));
    memcpy(&__bunki_patch2__, &stack_size, sizeof(uint32_t));
    memcpy(&__bunki_patch3__, &stack_size, sizeof(uint32_t));
done:
    ret |= patch_obj_mprotect(&__bunki_patch0__, sizeof(uint32_t), OBJ_RE);
    ret |= patch_obj_mprotect(&__bunki_patch1__, sizeof(uint32_t), OBJ_RE);
    ret |= patch_obj_mprotect(&__bunki_patch2__, sizeof(uint32_t), OBJ_RE);
    ret |= patch_obj_mprotect(&__bunki_patch3__, sizeof(uint32_t), OBJ_RE);
    return ret;
}
#endif

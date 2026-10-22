#include "Memory.h"

#include "Addresses.h"

#include <windows.h>

#include <climits>
#include <cstring>

// Functions with __try must not hold C++ objects with destructors (C2712), so each one
// below works on plain values only.

namespace mem {
namespace {

using CalcTotalCostToStateFn = uint32_t(__thiscall*)(uintptr_t self);

bool IsAccessViolation(DWORD code) {
    return code == EXCEPTION_ACCESS_VIOLATION || code == EXCEPTION_IN_PAGE_ERROR ||
           code == EXCEPTION_GUARD_PAGE;
}

}  // namespace

bool IsPlausiblePointer(uintptr_t p) {
    return p >= 0x10000 && p < 0x80000000 && (p & 3) == 0;
}

bool Read(uintptr_t address, void* out, size_t size) {
    if (address < 0x10000) return false;
    __try {
        std::memcpy(out, reinterpret_cast<const void*>(address), size);
        return true;
    } __except (IsAccessViolation(GetExceptionCode()) ? EXCEPTION_EXECUTE_HANDLER
                                                      : EXCEPTION_CONTINUE_SEARCH) {
        return false;
    }
}

bool ReadU32(uintptr_t address, uint32_t& out) {
    return Read(address, &out, sizeof(out));
}

bool ReadPointer(uintptr_t address, uintptr_t& out) {
    uint32_t value = 0;
    if (!ReadU32(address, value) || !IsPlausiblePointer(value)) return false;
    out = value;
    return true;
}

bool CallCalcTotalCostToState(uintptr_t pursuit, uint32_t& out) {
    if (!IsPlausiblePointer(pursuit)) return false;
    const auto fn = reinterpret_cast<CalcTotalCostToStateFn>(addr::kCalcTotalCostToState);
    __try {
        out = fn(pursuit);
        return true;
    } __except (IsAccessViolation(GetExceptionCode()) ? EXCEPTION_EXECUTE_HANDLER
                                                      : EXCEPTION_CONTINUE_SEARCH) {
        return false;
    }
}

bool AddInt32Saturating(uintptr_t address, int32_t amount, int32_t& before, int32_t& after) {
    if (!IsPlausiblePointer(address) || amount < 0) return false;
    auto* target = reinterpret_cast<volatile LONG*>(address);
    __try {
        for (;;) {
            const LONG old = *target;
            const long long sum = static_cast<long long>(old) + amount;
            const LONG desired = sum > INT_MAX ? INT_MAX : static_cast<LONG>(sum);
            if (InterlockedCompareExchange(target, desired, old) == old) {
                before = old;
                after = desired;
                return true;
            }
        }
    } __except (IsAccessViolation(GetExceptionCode()) ? EXCEPTION_EXECUTE_HANDLER
                                                      : EXCEPTION_CONTINUE_SEARCH) {
        return false;
    }
}

}  // namespace mem

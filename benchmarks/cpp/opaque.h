// opaque.h — optimisation barrier for the pure C++ floors.
//
// PROTOCPP_OPAQUE(x) makes the compiler treat `x` as read and rewritten
// by code it cannot see, so a loop that accumulates into `x` can be
// neither constant-folded nor collapsed into a closed form.
//
// GCC and Clang: an empty asm statement that takes `x` in a register,
// the original barrier (no instruction is emitted).
// MSVC: x64 has no inline asm, so `x` goes through a volatile load
// instead. That costs a store and a load per use, so the MSVC floor is
// somewhat higher than the GCC/Clang one; compare Windows numbers with
// Windows numbers only.
#pragma once

#if defined(_MSC_VER) && !defined(__clang__)
#define PROTOCPP_OPAQUE(x) ((x) = *static_cast<volatile decltype(x)*>(&(x)))
#else
#define PROTOCPP_OPAQUE(x) asm volatile("" : "+r"(x) :: "memory")
#endif

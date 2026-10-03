/*
 * Copyright (c) 2026 The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * LLM-generated
 */

/*
 * Compiler atomic helpers (__atomic_*_N and __sync_*_N) implemented by
 * locking interrupts, for cores where the atomic instructions cannot be used.
 *
 * These are the functions GCC calls when the target has no 'A' extension in
 * -march, and the ones prebuilt libraries may reference by name. Valid on a
 * single core only.
 *
 * Handler's comment:
 * Reason on BL702: Atomics instructions are only supported in the TCM, while the blob requires the
 * A extension to be enabled. This combines needing the A extension, and needing it disabled, so
 * the solution is to disable it but provide the atomic functions needed by the blob,
 * here it's __atomic_fetch_add_4.
 */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/irq.h>

/*
 * The helper names are compiler built-ins, so each function is defined under
 * a private C name and given its real symbol name with an asm label.
 *
 * All of them are weak: Zephyr's lib/cpp cpp_atomics.c already provides some
 * (__atomic_compare_exchange_1/2/4), and those take precedence.
 */
#define SYM(name) __asm__(#name) __attribute__((weak))

#define ATOMIC_FETCH_OP(n, type, opname, expr)                                                     \
	type atomic_fetch_##opname##_##n(volatile void *ptr, type val, int memorder)               \
		SYM(__atomic_fetch_##opname##_##n);                                                \
	type atomic_fetch_##opname##_##n(volatile void *ptr, type val, int memorder)               \
	{                                                                                          \
		volatile type *p = ptr;                                                            \
		unsigned int key = irq_lock();                                                     \
		type old = *p;                                                                     \
												   \
		ARG_UNUSED(memorder);                                                              \
		*p = (type)(expr);                                                                 \
		irq_unlock(key);                                                                   \
		return old;                                                                        \
	}                                                                                          \
	type atomic_##opname##_fetch_##n(volatile void *ptr, type val, int memorder)               \
		SYM(__atomic_##opname##_fetch_##n);                                                \
	type atomic_##opname##_fetch_##n(volatile void *ptr, type val, int memorder)               \
	{                                                                                          \
		volatile type *p = ptr;                                                            \
		unsigned int key = irq_lock();                                                     \
		type old = *p;                                                                     \
		type new = (type)(expr);                                                           \
												   \
		ARG_UNUSED(memorder);                                                              \
		*p = new;                                                                          \
		irq_unlock(key);                                                                   \
		return new;                                                                        \
	}                                                                                          \
	type sync_fetch_and_##opname##_##n(volatile void *ptr, type val)                           \
		SYM(__sync_fetch_and_##opname##_##n);                                              \
	type sync_fetch_and_##opname##_##n(volatile void *ptr, type val)                           \
	{                                                                                          \
		return atomic_fetch_##opname##_##n(ptr, val, 0);                                   \
	}                                                                                          \
	type sync_##opname##_and_fetch_##n(volatile void *ptr, type val)                           \
		SYM(__sync_##opname##_and_fetch_##n);                                              \
	type sync_##opname##_and_fetch_##n(volatile void *ptr, type val)                           \
	{                                                                                          \
		return atomic_##opname##_fetch_##n(ptr, val, 0);                                   \
	}

#define ATOMIC_COMMON(n, type)                                                                     \
	ATOMIC_FETCH_OP(n, type, add, old + val)                                                   \
	ATOMIC_FETCH_OP(n, type, sub, old - val)                                                   \
	ATOMIC_FETCH_OP(n, type, and, old & val)                                                   \
	ATOMIC_FETCH_OP(n, type, or, old | val)                                                    \
	ATOMIC_FETCH_OP(n, type, xor, old ^ val)                                                   \
	ATOMIC_FETCH_OP(n, type, nand, ~(old & val))                                               \
												   \
	type atomic_exchange_##n(volatile void *ptr, type val, int memorder)                       \
		SYM(__atomic_exchange_##n);                                                        \
	type atomic_exchange_##n(volatile void *ptr, type val, int memorder)                       \
	{                                                                                          \
		volatile type *p = ptr;                                                            \
		unsigned int key = irq_lock();                                                     \
		type old = *p;                                                                     \
												   \
		ARG_UNUSED(memorder);                                                              \
		*p = val;                                                                          \
		irq_unlock(key);                                                                   \
		return old;                                                                        \
	}                                                                                          \
												   \
	bool atomic_compare_exchange_##n(volatile void *ptr, void *expected, type desired,         \
					 bool weak, int success, int failure)                      \
		SYM(__atomic_compare_exchange_##n);                                                \
	bool atomic_compare_exchange_##n(volatile void *ptr, void *expected, type desired,         \
					 bool weak, int success, int failure)                      \
	{                                                                                          \
		volatile type *p = ptr;                                                            \
		type exp;                                                                          \
		unsigned int key = irq_lock();                                                     \
		type old = *p;                                                                     \
		bool ok;                                                                           \
												   \
		ARG_UNUSED(weak);                                                                  \
		ARG_UNUSED(success);                                                               \
		ARG_UNUSED(failure);                                                               \
		memcpy(&exp, expected, sizeof(exp));                                               \
		ok = (old == exp);                                                                 \
		if (ok) {                                                                          \
			*p = desired;                                                              \
		} else {                                                                           \
			memcpy(expected, &old, sizeof(old));                                       \
		}                                                                                  \
		irq_unlock(key);                                                                   \
		return ok;                                                                         \
	}                                                                                          \
												   \
	type sync_val_compare_and_swap_##n(volatile void *ptr, type oldval, type newval)           \
		SYM(__sync_val_compare_and_swap_##n);                                              \
	type sync_val_compare_and_swap_##n(volatile void *ptr, type oldval, type newval)           \
	{                                                                                          \
		(void)atomic_compare_exchange_##n(ptr, &oldval, newval, false, 0, 0);              \
		return oldval;                                                                     \
	}                                                                                          \
												   \
	bool sync_bool_compare_and_swap_##n(volatile void *ptr, type oldval, type newval)          \
		SYM(__sync_bool_compare_and_swap_##n);                                             \
	bool sync_bool_compare_and_swap_##n(volatile void *ptr, type oldval, type newval)          \
	{                                                                                          \
		return atomic_compare_exchange_##n(ptr, &oldval, newval, false, 0, 0);             \
	}                                                                                          \
												   \
	type sync_lock_test_and_set_##n(volatile void *ptr, type val)                              \
		SYM(__sync_lock_test_and_set_##n);                                                 \
	type sync_lock_test_and_set_##n(volatile void *ptr, type val)                              \
	{                                                                                          \
		return atomic_exchange_##n(ptr, val, 0);                                           \
	}

ATOMIC_COMMON(1, uint8_t)
ATOMIC_COMMON(2, uint16_t)
ATOMIC_COMMON(4, uint32_t)
ATOMIC_COMMON(8, uint64_t)

/*
 * 64-bit loads and stores are two instructions on a 32-bit core, so the
 * compiler calls out for them even when the smaller sizes are inlined.
 */
uint64_t atomic_load_8(const volatile void *ptr, int memorder) SYM(__atomic_load_8);
uint64_t atomic_load_8(const volatile void *ptr, int memorder)
{
	const volatile uint64_t *p = ptr;
	unsigned int key = irq_lock();
	uint64_t val = *p;

	ARG_UNUSED(memorder);
	irq_unlock(key);
	return val;
}

void atomic_store_8(volatile void *ptr, uint64_t val, int memorder) SYM(__atomic_store_8);
void atomic_store_8(volatile void *ptr, uint64_t val, int memorder)
{
	volatile uint64_t *p = ptr;
	unsigned int key = irq_lock();

	ARG_UNUSED(memorder);
	*p = val;
	irq_unlock(key);
}

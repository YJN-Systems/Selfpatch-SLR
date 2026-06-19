#include "spslr_env.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <string.h>
#include <time.h>
#include <stdarg.h>

/*
 * Userspace environment backend.
 *
 * This file provides allocation, randomness, address lookup, and text
 * permission changes using libc/Linux userspace facilities. A kernel version
 * should replace these hooks with kernel-native equivalents.
 */

#define PAGE_MASK ~(0x1000ull - 1)

static int spslr_env_get_prot(void *addr, int *prot)
{
	/*
	 * Discover the mapping that contains a patch address.
	 *
	 * Userspace must temporarily make the containing page writable before patching
	 * instruction immediates.
	 */

	FILE *f = fopen("/proc/self/maps", "r");
	if (!f)
		return -1;

	spslr_uintptr query = (spslr_uintptr)addr;
	char line[512];

	while (fgets(line, sizeof(line), f)) {
		spslr_uintptr start, end;
		char perms[5];

		if (sscanf(line, "%lx-%lx %4s", &start, &end, perms) != 3)
			continue;

		if (query < start || query >= end)
			continue;

		*prot = 0;

		if (perms[0] == 'r')
			*prot |= PROT_READ;
		if (perms[1] == 'w')
			*prot |= PROT_WRITE;
		if (perms[2] == 'x')
			*prot |= PROT_EXEC;

		fclose(f);
		return 0;
	}

	fclose(f);
	return -1;
}

static int spslr_env_poke_safe(void *dst, const void *src, spslr_u64 n)
{
	/*
	 * Temporarily relax page permissions for text patching.
	 *
	 * The caller is responsible for restoring executable/read-only permissions
	 * after the immediate bytes have been updated.
	 */

	int original_prot;
	if (spslr_env_get_prot(dst, &original_prot))
		return -1;

	spslr_u64 ptr_uint = (spslr_u64)dst;
	spslr_u64 ptr_page = ptr_uint & PAGE_MASK;
	spslr_u64 prot_size = n + (ptr_uint - ptr_page);

	int tmp_prot = original_prot | PROT_WRITE;

	if (tmp_prot != original_prot)
		mprotect((void *)ptr_page, prot_size, tmp_prot);

	spslr_env_memcpy(dst, src, n);

	if (tmp_prot != original_prot)
		mprotect((void *)ptr_page, prot_size, original_prot);

	return 0;
}

int spslr_env_poke_text_8(void *dst, spslr_u8 value)
{
	return spslr_env_poke_safe(dst, &value, sizeof(value));
}

int spslr_env_poke_text_16(void *dst, spslr_u16 value)
{
	return spslr_env_poke_safe(dst, &value, sizeof(value));
}

int spslr_env_poke_text_32(void *dst, spslr_u32 value)
{
	return spslr_env_poke_safe(dst, &value, sizeof(value));
}

int spslr_env_poke_text_64(void *dst, spslr_u64 value)
{
	return spslr_env_poke_safe(dst, &value, sizeof(value));
}

void *__init spslr_env_malloc(spslr_u64 n)
{
	return malloc(n);
}

void __init spslr_env_free(void *ptr, spslr_u64 n)
{
	(void)n;
	free(ptr);
}

int spslr_env_poke_data(void *dst, const void *src, spslr_u64 n)
{
	return spslr_env_poke_safe(dst, src, n);
}

void spslr_env_memset(void *dst, int v, spslr_u64 n)
{
	memset(dst, v, n);
}

void spslr_env_memcpy(void *dst, const void *src, spslr_u64 n)
{
	memcpy(dst, src, n);
}

int spslr_env_memcmp(const void *x, const void *y, spslr_u64 n)
{
	return memcmp(x, y, n);
}

/*
 * Randomness source for layout permutation.
 *
 * This is part of the environment layer so production/kernel integrations can
 * substitute a stronger or policy-approved entropy source.
 */

static int rand_initialized = 0;

static spslr_u32 __init random_u32(void)
{
	if (!rand_initialized) {
		srand(time(NULL));
		rand_initialized = 1;
	}

	return (spslr_u32)rand();
}

spslr_u64 __init spslr_env_random_u64(void)
{
	return (spslr_u64)random_u32() | ((spslr_u64)random_u32() << 32);
}

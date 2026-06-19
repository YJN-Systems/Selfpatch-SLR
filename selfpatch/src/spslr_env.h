#ifndef SPSLR_ENV_H
#define SPSLR_ENV_H

#include <stdint.h>
#include <stddef.h>

#ifndef __packed
#define __packed __attribute__((packed))
#endif

#ifndef __init
#define __init /* only required in kernel */
#endif

#ifndef __printf
#define __printf(fmt_pos, arg_pos) \
	__attribute__((format(printf, fmt_pos, arg_pos)))
#endif

#ifndef NULL
#define NULL ((void *)0)
#endif

typedef uint8_t spslr_u8;
typedef uint16_t spslr_u16;
typedef uint32_t spslr_u32;
typedef uint64_t spslr_u64;
typedef int32_t spslr_s32;
typedef int64_t spslr_s64;
typedef uintptr_t spslr_uintptr;

int spslr_env_poke_text_8(void *dst, spslr_u8 value);
int spslr_env_poke_text_16(void *dst, spslr_u16 value);
int spslr_env_poke_text_32(void *dst, spslr_u32 value);
int spslr_env_poke_text_64(void *dst, spslr_u64 value);
int spslr_env_poke_data(void *dst, const void *src, spslr_u64 n);
void *spslr_env_malloc(spslr_u64 n);
void spslr_env_free(void *ptr, spslr_u64 n);
void spslr_env_memset(void *dst, int v, spslr_u64 n);
void spslr_env_memcpy(void *dst, const void *src, spslr_u64 n);
int spslr_env_memcmp(const void *x, const void *y, spslr_u64 n);
spslr_u64 spslr_env_random_u64(void);

#endif

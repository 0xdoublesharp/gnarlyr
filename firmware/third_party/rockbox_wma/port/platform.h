#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#define ROCKBOX_LITTLE_ENDIAN
#define MEM_ALIGN_ATTR __attribute__((aligned(16)))
#define ICODE_ATTR
#define ICONST_ATTR
#define IBSS_ATTR
#define CONFIG_CPU 0
#define PP5022 1
#define PP5024 2
#define MCF5250 3
#define S5L8702 4
#define LIKELY(x) __builtin_expect(!!(x), 1)
#define UNLIKELY(x) __builtin_expect(!!(x), 0)
#define DEBUGF(...) ((void)0)
#define AV_INPUT_BUFFER_PADDING_SIZE 8
#define FF_INPUT_BUFFER_PADDING_SIZE 8
#define av_unused __attribute__((unused))
#define av_alias __attribute__((may_alias))
_Noreturn void wma_fail(void);
#if defined(__arm__)
#define WMA_WORKSPACE __attribute__((section(".wma")))
#else
#define WMA_WORKSPACE
#endif

/* SPDX-License-Identifier: GPL-2.0-only */
/* Lets kernel/platform/vdec-h264.c be compiled into vdec-lab: kernel types and helpers it uses, with
 * register access going through the driver's debugfs `reg` file. */
#ifndef VDEC_LAB_SHIM_H
#define VDEC_LAB_SHIM_H
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int32_t s32;
typedef uint32_t dma_addr_t;
typedef struct { int counter; } atomic_t;
struct page;
struct device;
struct clk;
struct dentry;
struct miscdevice { int unused; };
struct mutex { int unused; };
struct completion { int unused; };
struct delayed_work { int unused; };
#define __iomem
#define BIT(n) (1U << (n))
#define S32_MAX INT32_MAX
#define ALIGN(x, a) (((x) + (a) - 1) & ~((a) - 1))
#define min(a, b) ({ __typeof__(a) a_ = (a); __typeof__(b) b_ = (b); a_ < b_ ? a_ : b_; })
#define max(a, b) ({ __typeof__(a) a_ = (a); __typeof__(b) b_ = (b); a_ > b_ ? a_ : b_; })
#define min_t(type, a, b) min((type)(a), (type)(b))
#define max_t(type, a, b) max((type)(a), (type)(b))
#define swap(a, b) do { __typeof__(a) t_ = (a); (a) = (b); (b) = t_; } while (0)
#define sort(base, n, size, compare, exchange) qsort(base, n, size, compare)
#define kmalloc(size, flags) malloc(size)
#define kfree(pointer) free(pointer)
#define GFP_KERNEL 0

u32 lab_readl(uintptr_t address);
void lab_writel(u32 value, uintptr_t address);
double lab_now_us(void);
#define readl(address) lab_readl((uintptr_t)(address))
#define writel(value, address) lab_writel(value, (uintptr_t)(address))
#define readl_poll_timeout(address, value, condition, delay_us, timeout_us) \
	readl_poll_timeout_atomic(address, value, condition, delay_us, timeout_us)
#define readl_poll_timeout_atomic(address, value, condition, delay_us, timeout_us) ({ \
	double end_ = lab_now_us() + (timeout_us); \
	int result_; \
	for (;;) { \
		(value) = readl(address); \
		if (condition) { result_ = 0; break; } \
		if (lab_now_us() > end_) { \
			(value) = readl(address); \
			result_ = (condition) ? 0 : -ETIMEDOUT; \
			break; \
		} \
	} \
	result_; })
#endif

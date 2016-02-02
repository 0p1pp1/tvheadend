/*
 * MULTI2 Descrambling Library for BCAS
 *
 * Copyright (C) 2016 0p1pp1
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 * 
 */

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef BSD
#  include <sys/endian.h>
#  define be64toh(x) betoh64((x))
#elif defined(OS_MACOSX)
#  include <machine/endian.h>
#else
#  include <endian.h>
#endif

/* for ENABLE_UNALIGNED_ACCESS only */
#include "build.h"

#include "demulti2.h"

typedef uint8_t   u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;


typedef struct u32x2 {
#if (BYTE_ORDER == LITTLE_ENDIAN)
	u32 r;
	u32 l;
#else
	u32 l;
	u32 r;
#endif
} /* __attribute__ ((packed, aligned(4))) */ CORE_DATA;

typedef union demulti2_key {
	u64 whole;
	CORE_DATA sub;
} m2key_t;

struct demulti2_key_info {
	m2key_t k_scr[2];
	u32 wrk[2][8]; /* wrk[0][]:even, wrk[1][]:odd */
};

static const struct demulti2_param {
	int round;
	u32 k_sys[8];
	union demulti2_key cbc_init;
} Init_param = {
	.round = 4,
	.k_sys = { /* stored in native endian, not BE */
		0x36310466, 0x4b17ea5c, 0x32df9cf5, 0xc4c36c1b,
		0xec993921, 0x689d4bb7, 0xb74e4084, 0x0d2e7d98
	},
	.cbc_init.whole = 0xfe27199919690911ULL,
};

/* internal utility functions */

/* (un-aligned) byteswap functions */

#define is_aligned(POINTER)  (((uintptr_t)(const void *)(POINTER)) % 8 == 0)

#if BYTE_ORDER == LITTLE_ENDIAN
#  if ENABLE_UNALIGNED_ACCESS

static inline u64 BE64TOH(const u8 *src)
{
	return be64toh(*(u64 *)src);
}

static inline void HTOBE64(u8 *dst, u64 src)
{
	*(u64 *)dst = htobe64(src);
}

#  else /* !ENABLE_UNALIGNED_ACCESS */

static inline u64 BE64TOH(const u8 *src)
{
	if (is_aligned(src))
		return be64toh(*(u64 *)src);
	else {
		u64 n;

		memcpy(&n, src, sizeof(u64));
		return be64toh(n);
	}
}

static inline void HTOBE64(u8 *dst, u64 src)
{
	if (is_aligned(dst))
		*(u64 *)dst = htobe64(src);
	else {
		u64 n;

		n = htobe64(src);
		memcpy(dst, &n, sizeof(u64));
	}
}

#  endif /* ENABLE_UNALIGNED_ACCESS */
#else /* !(BYTE_ORDER == LITTLE_ENDIAN) */
#  if ENABLE_UNALIGNED_ACCESS

static inline u64 BE64TOH(const u8 *src)
{
	return (*(u64 *)src);
}

static inline void HTOBE64(u8 *dst, u64 src)
{
	*(u64 *)dst = src;
}

#  else /* !ENABLE_UNALIGNED_ACCESS */

static inline u64 BE64TOH(const u8 *src)
{
	if (is_aligned(src))
		return (*(u64 *)src);
	else {
		u64 n;

		memcpy(&n, src, sizeof(u64));
		return n;
	}
}

static inline void HTOBE64(u8 *dst, u64 src)
{
	if (is_aligned(dst))
		*(u64 *)dst = src;
	else
		memcpy(dst, &src, sizeof(u64));
}

#  endif /* ENABLE_UNALIGNED_ACCESS */
#endif /* BYTE_ORDER == LITTLE_ENDIAN */


static u32
left_rotate_uint32 (u32 val, u32 count)
{
	return ((val << count) | (val >> (32 - count)));
}


static void
core_pi1 (CORE_DATA * dst, CORE_DATA * src)
{
	dst->l = src->l;
	dst->r = src->r ^ src->l;
}


static void
core_pi2 (CORE_DATA * dst, CORE_DATA * src, u32 a)
{
	u32 t0, t1, t2;

	t0 = src->r + a;
	t1 = left_rotate_uint32 (t0, 1) + t0 - 1;
	t2 = left_rotate_uint32 (t1, 4) ^ t1;

	dst->l = src->l ^ t2;
	dst->r = src->r;
}


static void
core_pi3 (CORE_DATA * dst, CORE_DATA * src, u32 a, u32 b)
{
	u32 t0, t1, t2, t3, t4, t5;

	t0 = src->l + a;
	t1 = left_rotate_uint32 (t0, 2) + t0 + 1;
	t2 = left_rotate_uint32 (t1, 8) ^ t1;
	t3 = t2 + b;
	t4 = left_rotate_uint32 (t3, 1) - t3;
	t5 = left_rotate_uint32 (t4, 16) ^ (t4 | src->l);

	dst->l = src->l;
	dst->r = src->r ^ t5;
}


static void
core_pi4 (CORE_DATA * dst, CORE_DATA * src, u32 a)
{
	u32 t0, t1;

	t0 = src->r + a;
	t1 = left_rotate_uint32 (t0, 2) + t0 + 1;

	dst->l = src->l ^ t1;
	dst->r = src->r;
}


static void
core_schedule (u32 * work, const u32 * skey, CORE_DATA * dkey)
{
	CORE_DATA b1, b2, b3, b4, b5, b6, b7, b8, b9;

	core_pi1 (&b1, dkey);

	core_pi2 (&b2, &b1, skey[0]);
	work[0] = b2.l;

	core_pi3 (&b3, &b2, skey[1], skey[2]);
	work[1] = b3.r;

	core_pi4 (&b4, &b3, skey[3]);
	work[2] = b4.l;

	core_pi1 (&b5, &b4);
	work[3] = b5.r;

	core_pi2 (&b6, &b5, skey[4]);
	work[4] = b6.l;

	core_pi3 (&b7, &b6, skey[5], skey[6]);
	work[5] = b7.r;

	core_pi4 (&b8, &b7, skey[7]);
	work[6] = b8.l;

	core_pi1 (&b9, &b8);
	work[7] = b9.r;
}


static void
core_encrypt (CORE_DATA * dst, CORE_DATA * src, u32 * w, int round)
{
	int i;

	CORE_DATA tmp;

	dst->l = src->l;
	dst->r = src->r;
	for (i = 0; i < round; i++) {
		core_pi1 (&tmp, dst);
		core_pi2 (dst, &tmp, w[0]);
		core_pi3 (&tmp, dst, w[1], w[2]);
		core_pi4 (dst, &tmp, w[3]);
		core_pi1 (&tmp, dst);
		core_pi2 (dst, &tmp, w[4]);
		core_pi3 (&tmp, dst, w[5], w[6]);
		core_pi4 (dst, &tmp, w[7]);
	}
}


static void
core_decrypt (CORE_DATA * dst, CORE_DATA * src, u32 * w, int round)
{
	int i;

	CORE_DATA tmp;

	dst->l = src->l;
	dst->r = src->r;
	for (i = 0; i < round; i++) {
		core_pi4 (&tmp, dst, w[7]);
		core_pi3 (dst, &tmp, w[5], w[6]);
		core_pi2 (&tmp, dst, w[4]);
		core_pi1 (dst, &tmp);
		core_pi4 (&tmp, dst, w[3]);
		core_pi3 (dst, &tmp, w[1], w[2]);
		core_pi2 (&tmp, dst, w[0]);
		core_pi1 (dst, &tmp);
	}
}


static void
descramble (const u8 * ibuf, int len, u8 * obuf, u32 * prm, int round, u64 init)
{
	m2key_t src, dst, cbc;

	cbc.whole = init;

	while (len >= 8) {
		src.whole = BE64TOH (ibuf);
		core_decrypt (&dst.sub, &src.sub, prm, round);
		dst.whole ^= cbc.whole;
		cbc.whole = src.whole;
		HTOBE64 (obuf, dst.whole);
		len -= 8;
		ibuf += 8;
		obuf += 8;
	}

	if (len > 0) {
		int i;
		u64 t64;
		u8 *tmp = (u8 *) & t64;

		core_encrypt (&dst.sub, &cbc.sub, prm, round);
		t64 = BE64TOH ((u8 *) &dst.whole);

		for (i = 0; i < len; i++)
			obuf[i] = ibuf[i] ^ tmp[i];
	}

	return;
}


static void key_schedule(const unsigned char *data, int is_odd, void *keys)
{
	struct demulti2_key_info *kinfo = keys;
	m2key_t k;

	k.whole = BE64TOH(data);
	kinfo->k_scr[is_odd].whole = k.whole;
	core_schedule(kinfo->wrk[is_odd], Init_param.k_sys, &k.sub);
}


/*
 * exported functions
 */

int multi2_decrypt_packet(void *keys, unsigned char *packet)
{
	struct demulti2_key_info *kinfo = keys;
	uint8_t ca_flags;
	uint8_t *p;
	int len;

	ca_flags = (packet[3] >> 6);
	if (ca_flags == 0)
		return 0;
	else if (ca_flags == 1)
		return - EINVAL;

	p = packet + 4;
	len = 184;
	if (packet[3] & 0x20) {
		len -= p[0] + 1;
		p += p[0] + 1;
	}
	descramble(p, len, p, kinfo->wrk[ca_flags - 2],
			Init_param.round, Init_param.cbc_init.whole);
	packet[3] &= 0x3f;
	return 0;
}


void multi2_odd_key_set(const unsigned char *odd, void *keys)
{
	key_schedule(odd, 1, keys);
}


void multi2_even_key_set(const unsigned char *even, void *keys)
{
	key_schedule(even, 0, keys);
}


void *multi2_get_key_struct(void) {
	return malloc(sizeof(struct demulti2_key_info));
}


void multi2_free_key_struct(void *keys) {
	free(keys);
}

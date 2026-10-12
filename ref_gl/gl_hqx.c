/*
Copyright (C) 2013 Andrey Nazarov

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

/*
HQ2x filter
authors: byuu and blargg
license: public domain

note: this is a clean reimplementation of the original HQ2x filter, which was
written by Maxim Stepin (MaxSt). it is not 100% identical, but very similar.
*/

/* Adapted from pinned Q2PRO hq2x.c: R1Q2 scalar types and explicit thresholds.
 * The donor's rule table, edge/alpha tests and HQ2x/HQ4x kernels are retained. */
#include "gl_hqx.h"
#include <float.h>
#include <assert.h>

#if defined(_MSC_VER)
#define HQX_INLINE __inline
#if _MSC_VER >= 1300
#define HQX_NOINLINE __declspec(noinline)
#else
#define HQX_NOINLINE
#endif
#elif defined(__GNUC__)
#define HQX_INLINE inline
#define HQX_NOINLINE __attribute__((noinline))
#else
#define HQX_INLINE
#define HQX_NOINLINE
#endif
#define HQX_MASK (((uint64)0x00ff00ff << 32) | 0x00ff00ff)
typedef union { uint32 u32; byte u8[4]; } hqx_color_t;

static const byte hqTable[256] = {
    1, 1, 2,  4, 1, 1, 2,  4, 3,  5,  7,  8, 3,  5, 13, 15,
    1, 1, 2, 10, 1, 1, 2, 10, 3,  5,  8,  8, 3,  5,  6,  8,
    1, 1, 2,  4, 1, 1, 2,  4, 3,  5, 12, 14, 3,  5,  9, 16,
    1, 1, 2, 10, 1, 1, 2, 10, 3,  5,  9,  8, 3,  5,  6, 16,
    1, 1, 2,  4, 1, 1, 2,  4, 3, 11,  8,  8, 3, 11,  9,  8,
    1, 1, 2,  4, 1, 1, 2,  4, 3,  5,  9,  8, 3,  5,  9,  8,
    1, 1, 2,  4, 1, 1, 2,  4, 3, 11,  6,  8, 3, 11,  6, 16,
    1, 1, 2,  4, 1, 1, 2, 10, 3,  5,  9,  8, 3, 11,  6, 16,
    1, 1, 2,  4, 1, 1, 2,  4, 3,  5,  7,  8, 3,  5, 13, 15,
    1, 1, 2,  4, 1, 1, 2,  4, 3,  5,  9,  8, 3,  5,  9,  8,
    1, 1, 2,  4, 1, 1, 2,  4, 3,  5, 12, 14, 3,  5,  9, 16,
    1, 1, 2,  4, 1, 1, 2,  4, 3,  5,  9, 14, 3,  5,  6, 16,
    1, 1, 2,  4, 1, 1, 2,  4, 3,  5,  9,  8, 3,  5,  9, 15,
    1, 1, 2,  4, 1, 1, 2,  4, 3,  5,  9,  8, 3,  5,  6,  8,
    1, 1, 2,  4, 1, 1, 2,  4, 3,  5,  9,  8, 3,  5,  6, 16,
    1, 1, 2,  4, 1, 1, 2,  4, 3,  5,  6,  8, 3,  5,  6, 16,
};

static byte  rotTable[256];
static int32  yccTable[8][256];
static int32  maxY, maxCb, maxCr;

static HQX_NOINLINE int diff(uint32 A_u32, uint32 B_u32)
{
    hqx_color_t A, B;
    int32 a, b;

    A.u32 = A_u32;
    B.u32 = B_u32;

    if (A.u8[3] == 0 && B.u8[3] == 0)
        return 0;

    if (A.u8[3] == 0 || B.u8[3] == 0)
        return 1;

    a = yccTable[0][A.u8[0]] + yccTable[1][A.u8[1]] + yccTable[2][A.u8[2]];
    b = yccTable[0][B.u8[0]] + yccTable[1][B.u8[1]] + yccTable[2][B.u8[2]];
    if (abs(a - b) > maxY)
        return 1;

    a = yccTable[3][A.u8[0]] + yccTable[4][A.u8[1]] + yccTable[5][A.u8[2]];
    b = yccTable[3][B.u8[0]] + yccTable[4][B.u8[1]] + yccTable[5][B.u8[2]];
    if (abs(a - b) > maxCb)
        return 1;

    a = yccTable[5][A.u8[0]] + yccTable[6][A.u8[1]] + yccTable[7][A.u8[2]];
    b = yccTable[5][B.u8[0]] + yccTable[6][B.u8[1]] + yccTable[7][B.u8[2]];
    if (abs(a - b) > maxCr)
        return 1;

    return 0;
}

static HQX_INLINE int same(uint32 A, uint32 B)
{
    return !diff(A, B);
}

static HQX_INLINE uint64 grow(uint64 n)
{
    n |= n << 24;
    n &= HQX_MASK;
    return n;
}

static HQX_INLINE uint32 pack(uint64 n)
{
    n &= HQX_MASK;
    n |= n >> 24;
    return (uint32)n;
}

static HQX_NOINLINE uint32 blend_1_1(uint32 A, uint32 B)
{
    return pack((grow(A) + grow(B)) >> 1);
}

static HQX_NOINLINE uint32 blend_3_1(uint32 A, uint32 B)
{
    return pack((grow(A) * 3 + grow(B)) >> 2);
}

static HQX_NOINLINE uint32 blend_7_1(uint32 A, uint32 B)
{
    return pack((grow(A) * 7 + grow(B)) >> 3);
}

static HQX_NOINLINE uint32 blend_5_3(uint32 A, uint32 B)
{
    return pack((grow(A) * 5 + grow(B) * 3) >> 3);
}

static HQX_NOINLINE uint32 blend_2_1_1(uint32 A, uint32 B, uint32 C)
{
    return pack((grow(A) * 2 + grow(B) + grow(C)) >> 2);
}

static HQX_NOINLINE uint32 blend_5_2_1(uint32 A, uint32 B, uint32 C)
{
    return pack((grow(A) * 5 + grow(B) * 2 + grow(C)) >> 3);
}

static HQX_NOINLINE uint32 blend_6_1_1(uint32 A, uint32 B, uint32 C)
{
    return pack((grow(A) * 6 + grow(B) + grow(C)) >> 3);
}

static HQX_NOINLINE uint32 blend_2_3_3(uint32 A, uint32 B, uint32 C)
{
    return pack((grow(A) * 2 + (grow(B) + grow(C)) * 3) >> 3);
}

static HQX_NOINLINE uint32 blend_14_1_1(uint32 A, uint32 B, uint32 C)
{
    return pack((grow(A) * 14 + grow(B) + grow(C)) >> 4);
}

static HQX_NOINLINE uint32 hq2x_blend(int rule, uint32 E, uint32 A, uint32 B, uint32 D, uint32 F, uint32 H)
{
    switch (rule) {
    case 1:
        return blend_2_1_1(E, D, B);
    case 2:
        return blend_2_1_1(E, A, D);
    case 3:
        return blend_2_1_1(E, A, B);
    case 4:
        return blend_3_1(E, D);
    case 5:
        return blend_3_1(E, B);
    case 6:
        return blend_3_1(E, A);
    case 7:
        return same(B, D) ? blend_2_1_1(E, D, B) : blend_3_1(E, A);
    case 8:
        return same(B, D) ? blend_2_1_1(E, D, B) : E;
    case 9:
        return same(B, D) ? blend_6_1_1(E, D, B) : blend_3_1(E, A);
    case 10:
        return same(B, F) ? blend_5_2_1(E, B, D) : blend_3_1(E, D);
    case 11:
        return same(D, H) ? blend_5_2_1(E, D, B) : blend_3_1(E, B);
    case 12:
    case 13:
        return same(B, D) ? blend_2_3_3(E, D, B) : blend_3_1(E, A);
    case 14:
    case 15:
        return same(B, D) ? blend_2_3_3(E, D, B) : E;
    case 16:
        return same(B, D) ? blend_14_1_1(E, D, B) : E;
    default:
        assert(!"bad rule");
        return 0;
    }
}

static HQX_NOINLINE void hq4x_blend(int rule, uint32 *p00, uint32 *p01, uint32 *p10, uint32 *p11,
                                  uint32 E, uint32 A, uint32 B, uint32 D, uint32 F, uint32 H)
{
    switch (rule) {
    case 1:
        *p00 = blend_2_1_1(E, B, D);
        *p01 = blend_5_2_1(E, B, D);
        *p10 = blend_5_2_1(E, D, B);
        *p11 = blend_6_1_1(E, D, B);
        break;
    case 2:
        *p00 = blend_5_3(E, A);
        *p01 = blend_3_1(E, A);
        *p10 = blend_5_2_1(E, D, A);
        *p11 = blend_7_1(E, A);
        break;
    case 3:
        *p00 = blend_5_3(E, A);
        *p01 = blend_5_2_1(E, B, A);
        *p10 = blend_3_1(E, A);
        *p11 = blend_7_1(E, A);
        break;
    case 4:
        *p00 = blend_5_3(E, D);
        *p01 = blend_7_1(E, D);
        *p10 = blend_5_3(E, D);
        *p11 = blend_7_1(E, D);
        break;
    case 5:
        *p00 = blend_5_3(E, B);
        *p01 = blend_5_3(E, B);
        *p10 = blend_7_1(E, B);
        *p11 = blend_7_1(E, B);
        break;
    case 6:
        *p00 = blend_5_3(E, A);
        *p01 = blend_3_1(E, A);
        *p10 = blend_3_1(E, A);
        *p11 = blend_7_1(E, A);
        break;
    case 7:
        if (same(B, D)) {
            *p00 = blend_1_1(B, D);
            *p01 = blend_1_1(B, E);
            *p10 = blend_1_1(D, E);
            *p11 = E;
        } else {
            *p00 = blend_5_3(E, A);
            *p01 = blend_3_1(E, A);
            *p10 = blend_3_1(E, A);
            *p11 = blend_7_1(E, A);
        }
        break;
    case 8:
        if (same(B, D)) {
            *p00 = blend_1_1(B, D);
            *p01 = blend_1_1(B, E);
            *p10 = blend_1_1(D, E);
        } else {
            *p00 = E;
            *p01 = E;
            *p10 = E;
        }
        *p11 = E;
        break;
    case 9:
        if (same(B, D)) {
            *p00 = blend_2_1_1(E, B, D);
            *p01 = blend_3_1(E, B);
            *p10 = blend_3_1(E, D);
            *p11 = E;
        } else {
            *p00 = blend_5_3(E, A);
            *p01 = blend_3_1(E, A);
            *p10 = blend_3_1(E, A);
            *p11 = blend_7_1(E, A);
        }
        break;
    case 10:
        if (same(B, F)) {
            *p00 = blend_3_1(E, B);
            *p01 = blend_3_1(B, E);
        } else {
            *p00 = blend_5_3(E, D);
            *p01 = blend_7_1(E, D);
        }
        *p10 = blend_5_3(E, D);
        *p11 = blend_7_1(E, D);
        break;
    case 11:
        if (same(D, H)) {
            *p00 = blend_3_1(E, D);
            *p10 = blend_3_1(D, E);
        } else {
            *p00 = blend_5_3(E, B);
            *p10 = blend_7_1(E, B);
        }
        *p01 = blend_5_3(E, B);
        *p11 = blend_7_1(E, B);
        break;
    case 12:
        if (same(B, D)) {
            *p00 = blend_1_1(B, D);
            *p01 = blend_2_1_1(B, E, D);
            *p10 = blend_5_3(D, B);
            *p11 = blend_6_1_1(E, D, B);
        } else {
            *p00 = blend_5_3(E, A);
            *p01 = blend_3_1(E, A);
            *p10 = blend_3_1(E, A);
            *p11 = blend_7_1(E, A);
        }
        break;
    case 13:
        if (same(B, D)) {
            *p00 = blend_1_1(B, D);
            *p01 = blend_5_3(B, D);
            *p10 = blend_2_1_1(D, E, B);
            *p11 = blend_6_1_1(E, D, B);
        } else {
            *p00 = blend_5_3(E, A);
            *p01 = blend_3_1(E, A);
            *p10 = blend_3_1(E, A);
            *p11 = blend_7_1(E, A);
        }
        break;
    case 14:
        if (same(B, D)) {
            *p00 = blend_1_1(B, D);
            *p01 = blend_2_1_1(B, E, D);
            *p10 = blend_5_3(D, B);
            *p11 = blend_6_1_1(E, D, B);
        } else {
            *p00 = E;
            *p01 = E;
            *p10 = E;
            *p11 = E;
        }
        break;
    case 15:
        if (same(B, D)) {
            *p00 = blend_1_1(B, D);
            *p01 = blend_5_3(B, D);
            *p10 = blend_2_1_1(D, E, B);
            *p11 = blend_6_1_1(E, D, B);
        } else {
            *p00 = E;
            *p01 = E;
            *p10 = E;
            *p11 = E;
        }
        break;
    case 16:
        if (same(B, D))
            *p00 = blend_2_1_1(E, B, D);
        else
            *p00 = E;
        *p01 = E;
        *p10 = E;
        *p11 = E;
        break;
    default:
        assert(!"bad rule");
    }
}

void GL_HQ2x(uint32 *output, const uint32 *input, int width, int height)
{
    int x, y;

    if (!output || !input || width < 1 || height < 1) return;

    for (y = 0; y < height; y++) {
        const uint32 *in = input + y * width;
        uint32 *out0 = output + (y * 2 + 0) * width * 2;
        uint32 *out1 = output + (y * 2 + 1) * width * 2;

        int prevline = (y == 0 ? 0 : width);
        int nextline = (y == height - 1 ? 0 : width);

        for (x = 0; x < width; x++) {
            int prev = (x == 0 ? 0 : 1);
            int next = (x == width - 1 ? 0 : 1);

            uint32 A = *(in - prevline - prev);
            uint32 B = *(in - prevline);
            uint32 C = *(in - prevline + next);
            uint32 D = *(in - prev);
            uint32 E = *(in);
            uint32 F = *(in + next);
            uint32 G = *(in + nextline - prev);
            uint32 H = *(in + nextline);
            uint32 I = *(in + nextline + next);

            int pattern;
            pattern  = diff(E, A) << 0;
            pattern |= diff(E, B) << 1;
            pattern |= diff(E, C) << 2;
            pattern |= diff(E, D) << 3;
            pattern |= diff(E, F) << 4;
            pattern |= diff(E, G) << 5;
            pattern |= diff(E, H) << 6;
            pattern |= diff(E, I) << 7;

            *(out0 + 0) = hq2x_blend(hqTable[pattern], E, A, B, D, F, H); pattern = rotTable[pattern];
            *(out0 + 1) = hq2x_blend(hqTable[pattern], E, C, F, B, H, D); pattern = rotTable[pattern];
            *(out1 + 1) = hq2x_blend(hqTable[pattern], E, I, H, F, D, B); pattern = rotTable[pattern];
            *(out1 + 0) = hq2x_blend(hqTable[pattern], E, G, D, H, B, F);

            in++;
            out0 += 2;
            out1 += 2;
        }
    }
}

void GL_HQ4x(uint32 *output, const uint32 *input, int width, int height)
{
    int x, y;

    if (!output || !input || width < 1 || height < 1) return;

    for (y = 0; y < height; y++) {
        const uint32 *in = input + y * width;
        uint32 *out0 = output + (y * 4 + 0) * width * 4;
        uint32 *out1 = output + (y * 4 + 1) * width * 4;
        uint32 *out2 = output + (y * 4 + 2) * width * 4;
        uint32 *out3 = output + (y * 4 + 3) * width * 4;

        int prevline = (y == 0 ? 0 : width);
        int nextline = (y == height - 1 ? 0 : width);

        for (x = 0; x < width; x++) {
            int prev = (x == 0 ? 0 : 1);
            int next = (x == width - 1 ? 0 : 1);

            uint32 A = *(in - prevline - prev);
            uint32 B = *(in - prevline);
            uint32 C = *(in - prevline + next);
            uint32 D = *(in - prev);
            uint32 E = *(in);
            uint32 F = *(in + next);
            uint32 G = *(in + nextline - prev);
            uint32 H = *(in + nextline);
            uint32 I = *(in + nextline + next);

            int pattern;
            pattern  = diff(E, A) << 0;
            pattern |= diff(E, B) << 1;
            pattern |= diff(E, C) << 2;
            pattern |= diff(E, D) << 3;
            pattern |= diff(E, F) << 4;
            pattern |= diff(E, G) << 5;
            pattern |= diff(E, H) << 6;
            pattern |= diff(E, I) << 7;

            hq4x_blend(hqTable[pattern], out0 + 0, out0 + 1, out1 + 0, out1 + 1, E, A, B, D, F, H); pattern = rotTable[pattern];
            hq4x_blend(hqTable[pattern], out0 + 3, out1 + 3, out0 + 2, out1 + 2, E, C, F, B, H, D); pattern = rotTable[pattern];
            hq4x_blend(hqTable[pattern], out3 + 3, out3 + 2, out2 + 3, out2 + 2, E, I, H, F, D, B); pattern = rotTable[pattern];
            hq4x_blend(hqTable[pattern], out3 + 0, out2 + 0, out3 + 1, out2 + 1, E, G, D, H, B, F);

            in++;
            out0 += 4;
            out1 += 4;
            out2 += 4;
            out3 += 4;
        }
    }
}

#define FIX(x)      (int)((x) * (1 << 16))

static int32 HQx_Threshold(float value, float fallback)
{
    if (!(value >= -FLT_MAX && value <= FLT_MAX)) value = fallback;
    if (value < 0) value = 0;
    if (value > 256) value = 256;
    return FIX(value);
}

void GL_InitHQx(float y, float cb, float cr)
{
    int n;

    maxY  = HQx_Threshold(y, 48);
    maxCb = HQx_Threshold(cb, 7);
    maxCr = HQx_Threshold(cr, 6);

    for (n = 0; n < 256; n++) {
        rotTable[n] = (byte)(((n >> 2) & 0x11) | ((n << 2) & 0x88)
                    | ((n & 0x01) << 5) | ((n & 0x08) << 3)
                    | ((n & 0x10) >> 3) | ((n & 0x80) >> 5));
    }

    // libjpeg YCbCr coefficients
    for (n = 0; n < 256; n++) {
        yccTable[0][n] =  FIX(0.29900f) * n;
        yccTable[1][n] =  FIX(0.58700f) * n;
        yccTable[2][n] =  FIX(0.11400f) * n;
        yccTable[3][n] = -FIX(0.16874f) * n;
        yccTable[4][n] = -FIX(0.33126f) * n;
        yccTable[5][n] =  FIX(0.50000f) * n;
        yccTable[6][n] = -FIX(0.41869f) * n;
        yccTable[7][n] = -FIX(0.08131f) * n;
    }
}

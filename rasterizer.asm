;******************************************************************************
;* rasterizer.asm: SSE2 tile rasterization functions
;******************************************************************************
;* Copyright (C) 2014 Vabishchevich Nikolay <vabnick@gmail.com>
;*
;* This file is part of libass.
;*
;* Permission to use, copy, modify, and distribute this software for any
;* purpose with or without fee is hereby granted, provided that the above
;* copyright notice and this permission notice appear in all copies.
;*
;* THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
;* WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
;* MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
;* ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
;* WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
;* ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
;* OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
;******************************************************************************

%include "x86inc.asm"

SECTION_RODATA 32

words_index: dw 0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0A,0x0B,0x0C,0x0D,0x0E,0x0F
words_tile16: dw 1024,1024,1024,1024,1024,1024,1024,1024,1024,1024,1024,1024,1024,1024,1024,1024
words_tile32: dw 512,512,512,512,512,512,512,512,512,512,512,512,512,512,512,512

SECTION .text

;------------------------------------------------------------------------------
; MUL reg, num
;------------------------------------------------------------------------------

%macro MUL 2
%if (%2) == 0
    xor %1, %1
%elif (%2) == 1
%elif (%2) == 2
    add %1, %1  ; lea %1, [%1 + %1]
%elif (%2) == 3
    lea %1, [%1 + 2 * %1]
%elif (%2) == 4
    lea %1, [4 * %1]  ; shl %1, 2
%elif (%2) == 5
    lea %1, [%1 + 4 * %1]
%elif (%2) == 8
    lea %1, [8 * %1]  ; shl %1, 3
%elif (%2) == 9
    lea %1, [%1 + 8 * %1]
%elif (%2) == 16
    shl %1, 4
%elif (%2) == 32
    shl %1, 5
%elif (%2) == 64
    shl %1, 6
%elif (%2) == 128
    shl %1, 7
%elif (%2) == 256
    shl %1, 8
%else
    imul %1, %2
%endif
%endmacro

;------------------------------------------------------------------------------
; BCASTW m_dst, r_src
;------------------------------------------------------------------------------

%macro BCASTW 2
    movd xm%1, %2
%if mmsize == 32
    vpbroadcastw m%1, xm%1
%elif mmsize == 16
    punpcklwd m%1, m%1
    pshufd m%1, m%1, q0000
%endif
%endmacro

;------------------------------------------------------------------------------
; PABSW m_reg, m_tmp
;------------------------------------------------------------------------------

%macro PABSW 2
%if cpuflag(ssse3)
    pabsw m%1, m%1
%else
    pxor m%2, m%2
    psubw m%2, m%1
    pmaxsw m%1, m%2
%endif
%endmacro

;------------------------------------------------------------------------------
; FILL_LINE r_dst, src, size
;------------------------------------------------------------------------------

%macro FILL_LINE 3
%if ((%3) & (mmsize - 1)) == 0
    %assign %%i 0
    %rep (%3) / mmsize
        mova [%1 + %%i], m%2
    %assign %%i %%i + mmsize
    %endrep
%elif (%3) == 16
    mova [%1], xm%2
%else
    %error "invalid line size"
%endif
%endmacro

;------------------------------------------------------------------------------
; FILL_SOLID_TILE tile_order, suffix
; void fill_solid_tile%2(uint8_t *buf, ptrdiff_t stride);
;------------------------------------------------------------------------------

%macro FILL_SOLID_TILE 2
cglobal fill_solid_tile%2, 2,2,1
    pcmpeqd m0, m0
%rep (1 << %1) - 1
    FILL_LINE r0, 0, 1 << %1
    add r0, r1
%endrep
    FILL_LINE r0, 0, 1 << %1
    RET
%endmacro

INIT_XMM sse2
FILL_SOLID_TILE 4,16
FILL_SOLID_TILE 5,32
INIT_YMM avx2
FILL_SOLID_TILE 4,16
FILL_SOLID_TILE 5,32

;------------------------------------------------------------------------------
; CALC_LINE tile_order, m_dst, m_src, m_delta, m_zero, m_full, m_tmp
;------------------------------------------------------------------------------

%macro CALC_LINE 7
    paddw m%7, m%3, m%4
    pmaxsw m%2, m%3, m%5
    pmaxsw m%7, m%5
    pminsw m%2, m%6
    pminsw m%7, m%6
    paddw m%2, m%7
    psraw m%2, 7 - %1
%endmacro

;------------------------------------------------------------------------------
; DEF_A_SHIFT tile_order
;------------------------------------------------------------------------------

%macro DEF_A_SHIFT 1
%if mmsize >= (2 << %1)
    %define a_shift 0
%elif mmsize == 32
    %define a_shift 4
%elif mmsize == 16
    %define a_shift 3
%else
    %error "invalid mmsize"
%endif
%endmacro

;------------------------------------------------------------------------------
; FILL_HALFPLANE_TILE tile_order, suffix
; void fill_halfplane_tile%2(uint8_t *buf, ptrdiff_t stride,
;                            int32_t a, int32_t b, int64_t c, int32_t scale);
;------------------------------------------------------------------------------

%macro FILL_HALFPLANE_TILE 2
    DEF_A_SHIFT %1
%if ARCH_X86_64 && a_shift
cglobal fill_halfplane_tile%2, 6,7,9
%else
cglobal fill_halfplane_tile%2, 6,7,8
%endif
%if a_shift == 0
    SWAP 3, 8
%endif

%if ARCH_X86_64
    movsxd r2, r2d  ; a
    movsxd r3, r3d  ; b
    sar r4, 7 + %1  ; c >> (tile_order + 7)
    movsxd r5, r5d  ; scale
    mov r6, 1 << (45 + %1)
    imul r2, r5
    add r2, r6
    sar r2, 46 + %1  ; aa
    imul r3, r5
    add r3, r6
    sar r3, 46 + %1  ; bb
    imul r4, r5
    shr r6, 1 + %1
    add r4, r6
    sar r4, 45  ; cc
%else
    mov r0d, r4m  ; c_lo
    mov r2d, r5m  ; c_hi
    mov r1d, r6m  ; scale
    mov r5d, 1 << 12
    shr r0d, 7 + %1
    shl r2d, 25 - %1
    or r0d, r2d  ; r0d (eax) = c >> (tile_order + 7)
    imul r1d  ; r2d (edx) = (c >> ...) * scale >> 32
    add r2d, r5d
    sar r2d, 13
    mov r4d, r2d  ; cc
    shl r5d, 1 + %1
    mov r0d, r3m  ; r0d (eax) = b
    imul r1d  ; r2d (edx) = b * scale >> 32
    add r2d, r5d
    sar r2d, 14 + %1
    mov r3d, r2d  ; bb
    mov r0d, r2m  ; r0d (eax) = a
    imul r1d  ; r2d (edx) = a * scale >> 32
    add r2d, r5d
    sar r2d, 14 + %1  ; aa
    mov r0d, r0m
    mov r1d, r1m
%endif
    add r4d, 1 << (13 - %1)
    mov r6d, r2d
    add r6d, r3d
    sar r6d, 1
    sub r4d, r6d

    BCASTW 1, r4d  ; cc
    BCASTW 2, r2d  ; aa
%if a_shift
    psllw m3, m2, a_shift  ; aa * (mmsize / 2)
%endif
    pmullw m2, [words_index]
    psubw m1, m2  ; cc - aa * i

    mov r4d, r2d  ; aa
    mov r6d, r4d
    sar r6d, 31
    xor r4d, r6d
    sub r4d, r6d  ; abs_a
    mov r5d, r3d  ; bb
    mov r6d, r5d
    sar r6d, 31
    xor r5d, r6d
    sub r5d, r6d  ; abs_b
    cmp r4d, r5d
    cmovg r4d, r5d
    add r4d, 2
    sar r4d, 2  ; delta
    BCASTW 2, r4d
    psubw m1, m2  ; c1 = cc - aa * i - delta
    paddw m2, m2  ; 2 * delta

%if a_shift
    MUL r2d, (1 << %1) - (mmsize / 2)
    sub r3d, r2d  ; bb - (tile_size - mmsize / 2) * aa
%endif
%if ARCH_X86_64 || a_shift == 0
    BCASTW 8, r3d
%endif

    pxor m0, m0
    mova m4, [words_tile%2]
    mov r2d, (1 << %1)
    jmp .loop_entry

.loop_start
    add r0, r1
%if ARCH_X86_64 || a_shift == 0
    psubw m1, m8
%else
    BCASTW 7, r3d
    psubw m1, m7
%endif
.loop_entry
%assign i 0
%rep (1 << %1) / mmsize
%if i
    psubw m1, m3
%endif
    CALC_LINE %1, 5, 1,2, 0,4, 7
    psubw m1, m3
    CALC_LINE %1, 6, 1,2, 0,4, 7
    packuswb m5, m6
%if mmsize == 32
    vpermq m5, m5, q3120
%endif
    mova [r0 + i], m5
%assign i i + mmsize
%endrep
%if (1 << %1) < mmsize
    CALC_LINE %1, 5, 1,2, 0,4, 7
    packuswb m5, m6
    vpermq m5, m5, q3120
    mova [r0 + i], xm5
%endif
    sub r2d,1
    jnz .loop_start
    RET
%endmacro

INIT_XMM sse2
FILL_HALFPLANE_TILE 4,16
FILL_HALFPLANE_TILE 5,32
INIT_YMM avx2
FILL_HALFPLANE_TILE 4,16
FILL_HALFPLANE_TILE 5,32

;------------------------------------------------------------------------------
; struct segment
; {
;     int64_t c;
;     int32_t a, b, scale, flags;
;     int32_t x_min, x_max, y_min, y_max;
; };
;------------------------------------------------------------------------------

struc line
    .c: resq 1
    .a: resd 1
    .b: resd 1
    .scale: resd 1
    .flags: resd 1
    .x_min: resd 1
    .x_max: resd 1
    .y_min: resd 1
    .y_max: resd 1
endstruc

;------------------------------------------------------------------------------
; ZEROFILL dst, size, tmp1
;------------------------------------------------------------------------------

%macro ZEROFILL 3
%assign %%n 128 / mmsize
    mov %3, (%2) / 128
%%zerofill_loop:
%assign %%i 0
%rep %%n
    mova [%1 + %%i], mm_zero
%assign %%i %%i + mmsize
%endrep
    add %1, 128
    sub %3, 1
    jnz %%zerofill_loop
%assign %%i 0
%rep ((%2) / mmsize) & (%%n - 1)
    mova [%1 + %%i], mm_zero
%assign %%i %%i + mmsize
%endrep
%endmacro

;------------------------------------------------------------------------------
; CALC_DELTA_FLAG res, line, tmp1, tmp2
;------------------------------------------------------------------------------

%macro CALC_DELTA_FLAG 4
    mov %3d, [%2 + line.flags]
    xor %4d, %4d
    cmp %4d, [%2 + line.x_min]
    cmovz %4d, %3d
    xor %1d, %1d
    test %3d, 2  ; SEGFLAG_UR_DL
    cmovnz %1d, %4d
    shl %3d, 2
    xor %1d, %3d
    and %4d, 4
    and %1d, 4
    lea %1d, [%1d + 2 * %1d]
    xor %1d, %4d  ; bit 3 - dn_delta, bit 2 - up_delta
%endmacro

;------------------------------------------------------------------------------
; UPDATE_DELTA up/dn, dst, flag, pos, tmp
;------------------------------------------------------------------------------

%macro UPDATE_DELTA 5
%ifidn %1, up
    %define %%op add
    %define %%opi sub
    %assign %%flag 1 << 2
%elifidn %1, dn
    %define %%op sub
    %define %%opi add
    %assign %%flag 1 << 3
%else
    %error "up/dn expected"
%endif

    test %3d, %%flag
    jz %%skip
    lea %5d, [4 * %4d - 256]
    %%opi [%2], %5w
    lea %5d, [4 * %4d]
    %%op [%2 + 2], %5w
%%skip:
%endmacro

;------------------------------------------------------------------------------
; CALC_VBA tile_order, b
;------------------------------------------------------------------------------

%macro CALC_VBA 2
    BCASTW m_vba, %2d
%rep (2 << %1) / mmsize - 1
    psubw mm_vba, mm_van  ; b - (tile_size - (mmsize / 2)) * a
%endrep
%endmacro

;------------------------------------------------------------------------------
; FILL_BORDER_LINE tile_order, res, abs_a(abs_ab), b, [abs_b], size, sum,
;                  tmp8, tmp9, mt10, mt11, mt12, mt13, mt14, [mt15]
;------------------------------------------------------------------------------

%macro FILL_BORDER_LINE 15
    mov %8d, %6d
    shl %8d, 8 - %1  ; size << (8 - tile_order)
    xor %9d, %9d
%if ARCH_X86_64
    sub %8d, %3d  ; abs_a
    cmovg %8d, %9d
    add %8d, 1 << (14 - %1)
    shl %8d, 2 * %1 - 5  ; w
    BCASTW %15, %8d

    mov %9d, %5d  ; abs_b
    imul %9d, %6d
    sar %9d, 6  ; dc_b
    cmp %9d, %3d  ; abs_a
    cmovg %9d, %3d
%else
    sub %8w, %3w  ; abs_a
    cmovg %8d, %9d
    add %8w, 1 << (14 - %1)
    shl %8d, 2 * %1 - 5  ; w

    mov %9d, %3d  ; abs_ab
    shr %9d, 16  ; abs_b
    imul %9d, %6d
    sar %9d, 6  ; dc_b
    cmp %9w, %3w
    cmovg %9w, %3w
%endif
    add %9d, 2
    sar %9d, 2  ; dc

    imul %7d, %4d  ; sum * b
    sar %7d, 7  ; avg * b
    add %7d, %9d  ; avg * b + dc
    add %9d, %9d  ; 2 * dc

    imul %7d, %8d
    sar %7d, 16
    sub %7d, %6d  ; -offs1
    BCASTW %10, %7d
    imul %9d, %8d
    sar %9d, 16  ; offs2 - offs1
    BCASTW %11, %9d
    add %6d, %6d
    BCASTW %12, %6d

%assign %%i 0
%rep (2 << %1) / mmsize
%if %%i
    psubw mm_c, mm_van
%endif
%if ARCH_X86_64
    pmulhw m%13, mm_c, m%15
%else
    BCASTW %14, %8d
    pmulhw m%13, mm_c, m%14
%endif
    psubw m%13, m%10  ; c1
    paddw m%14, m%13, m%11  ; c2
    pmaxsw m%13, mm_zero
    pmaxsw m%14, mm_zero
    pminsw m%13, m%12
    pminsw m%14, m%12
    paddw m%13, m%14
    paddw m%13, [%2 + %%i]
    mova [%2 + %%i], m%13
%assign %%i %%i + mmsize
%endrep
%endmacro

;------------------------------------------------------------------------------
; SAVE_RESULT tile_order, buf, stride, src, delta,
;             tmp6, tmp7, mt8, mt9, mt10, mt11
;------------------------------------------------------------------------------

%macro SAVE_RESULT 11
    mov %6d, 1 << %1
    xor %7d, %7d
%%save_loop:
    add %7w, [%5]
    BCASTW %10, %7d
    add %5, 2

%assign %%i 0
%rep (1 << %1) / mmsize
    paddw m%8, m%10, [%4 + 2 * %%i]
    PABSW %8, %11
    paddw m%9, m%10, [%4 + 2 * %%i + mmsize]
    PABSW %9, %11
    packuswb m%8, m%9
%if mmsize == 32
    vpermq m%8, m%8, q3120
%endif
    mova [%2 + %%i], m%8
%assign %%i %%i + mmsize
%endrep
%if (1 << %1) < mmsize
    paddw m%8, m%10, [%4 + 2 * %%i]
    PABSW %8, %11
    packuswb m%8, m%8
    vpermq m%8, m%8, q3120
    mova [%2 + %%i], xm%8
%endif

    add %2, %3
    add %4, 2 << %1
    sub %6d, 1
    jnz %%save_loop
%endmacro

;------------------------------------------------------------------------------
; GET_RES_ADDR dst
; CALC_RES_ADDR tile_order, dst/index, tmp, [skip_calc]
;------------------------------------------------------------------------------

%macro GET_RES_ADDR 1
%if HAVE_ALIGNED_STACK
    mov %1, rstk
%else
    lea %1, [rstk + mmsize - 1]
    and %1, ~(mmsize - 1)
%endif
%endmacro

%macro CALC_RES_ADDR 3-4 noskip
    shl %2d, 1 + %1
%if HAVE_ALIGNED_STACK
    add %2, rstk
%else
%ifidn %4, noskip
    lea %3, [rstk + mmsize - 1]
    and %3, ~(mmsize - 1)
%endif
    add %2, %3
%endif
%endmacro

;------------------------------------------------------------------------------
; FILL_GENERIC_TILE tile_order, suffix
; void fill_generic_tile%2(uint8_t *buf, ptrdiff_t stride,
;                          const struct segment *line, size_t n_lines,
;                          int winding);
;------------------------------------------------------------------------------

%macro FILL_GENERIC_TILE 2
    ; t3=line t4=dn/cur t5=up/end t6=up_pos t7=dn_pos
    ; t8=a/abs_a/abs_ab t9=b t10=c/abs_b
%if ARCH_X86_64
    DECLARE_REG_TMP 10,11,5,2, 4,9,6,7, 8,12,13
%else
    DECLARE_REG_TMP 0,1,5,3, 4,6,6,0, 2,3,5
%endif

    %assign tile_size 1 << %1
    %assign delta_offs 2 * tile_size * tile_size
    %assign alloc_size 2 * tile_size * (tile_size + 1) + 4
    %assign buf_size 2 * tile_size * (tile_size + 1)
    DEF_A_SHIFT %1

%if ARCH_X86_64
    %define m_zero  6
    %define m_full  7
    %define mm_index m8
    %define m_c     9
    %define m_vba   10
%if a_shift
    %define m_van   11
cglobal fill_generic_tile%2, 5,14,12
%else
cglobal fill_generic_tile%2, 5,14,11
%endif

%else
    %define m_zero  5
    %define m_full  4  ; tmp
    %define mm_index [words_index]
    %define m_c     7
%if a_shift
    %define m_van   6
    %define m_vba   3  ; tmp
%else
    %define m_vba   6
%endif

    %assign alloc_size alloc_size + 8
cglobal fill_generic_tile%2, 0,7,8
%endif

    %define mm_zero  m %+ m_zero
    %define mm_full  m %+ m_full
    %define mm_c     m %+ m_c
    %define mm_vba   m %+ m_vba
%if a_shift
    %define mm_van   m %+ m_van
%endif

%if HAVE_ALIGNED_STACK
    %assign alloc_size alloc_size + stack_offset + gprsize + (mmsize - 1)
    %assign alloc_size (alloc_size & ~(mmsize - 1)) - stack_offset - gprsize
%else
    %assign alloc_size alloc_size + 2 * mmsize
    %assign delta_offs delta_offs + mmsize
    %assign buf_size buf_size + mmsize
%endif
    SUB rstk, alloc_size

    GET_RES_ADDR t0
    pxor mm_zero, mm_zero
    ZEROFILL t0, buf_size, t1

%if ARCH_X86_64 == 0
    mov r4d, r4m
%endif
    shl r4d, 8
    mov [rstk + delta_offs], r4w

%if ARCH_X86_64
    mova mm_index, [words_index]
    mova mm_full, [words_tile%2]
    %define up_addr t5
%else
    %define up_addr [rstk + delta_offs + 2 * tile_size + 4]
    %define up_pos [rstk + delta_offs + 2 * tile_size + 8]
%endif

.line_loop
%if ARCH_X86_64 == 0
    mov t3, r2m
    lea t0, [t3 + line_size]
    mov r2m, t0
%endif
    CALC_DELTA_FLAG t0, t3, t1,t2

    mov t4d, [t3 + line.y_min]
    mov t2d, [t3 + line.y_max]
%if ARCH_X86_64
    mov t8d, t4d
    mov t6d, t4d
    and t6d, 63  ; dn_pos
    shr t4d, 6  ; dn
    mov t5d, t2d
    mov t7d, t2d
    and t7d, 63  ; up_pos
    shr t5d, 6  ; up

    UPDATE_DELTA dn, rstk + 2 * t4 + delta_offs, t0,t6, t1
    UPDATE_DELTA up, rstk + 2 * t5 + delta_offs, t0,t7, t1
    cmp t8d, t2d
%else
    lea t1d, [t0d + 1]
    cmp t4d, t2d
    cmovnz t0d, t1d  ; bit 0 -- not horz line

    mov t6d, t2d
    and t6d, 63  ; up_pos
    shr t2d, 6  ; up
    UPDATE_DELTA up, rstk + 2 * t2 + delta_offs, t0,t6, t1

    CALC_RES_ADDR %1, t2, t1
    mov up_addr, t2
    mov up_pos, t6d

    mov t6d, t4d
    and t6d, 63  ; dn_pos
    shr t4d, 6  ; dn
    UPDATE_DELTA dn, rstk + 2 * t4 + delta_offs, t0,t6, t1
    test t0d, 1
%endif
    jz .end_line_loop

%if ARCH_X86_64
    movsxd t8, dword [t3 + line.a]
    movsxd t9, dword [t3 + line.b]
    mov t10, [t3 + line.c]
    sar t10, 7 + %1  ; c >> (tile_order + 7)
    movsxd t0, dword [t3 + line.scale]
    mov t1, 1 << (45 + %1)
    imul t8, t0
    add t8, t1
    sar t8, 46 + %1  ; a
    imul t9, t0
    add t9, t1
    sar t9, 46 + %1  ; b
    imul t10, t0
    shr t1, 1 + %1
    add t10, t1
    sar t10, 45  ; c
%else
    mov r0d, [t3 + line.c]
    mov r2d, [t3 + line.c + 4]
    mov r1d, [t3 + line.scale]
    shr r0d, 7 + %1
    shl r2d, 25 - %1
    or r0d, r2d  ; r0d (eax) = c >> (tile_order + 7)
    imul r1d  ; r2d (edx) = (c >> ...) * scale >> 32
    add r2d, 1 << 12
    sar r2d, 13
    mov t10d, r2d  ; c
    mov r0d, [t3 + line.b]  ; r0d (eax)
    imul r1d  ; r2d (edx) = b * scale >> 32
    add r2d, 1 << (13 + %1)
    sar r2d, 14 + %1
    mov r0d, [t3 + line.a]  ; r0d (eax)
    mov t9d, r2d  ; b (overrides t3)
    imul r1d  ; r2d (edx) = a * scale >> 32
    add r2d, 1 << (13 + %1)
    sar r2d, 14 + %1  ; a (t8d)
%endif

    mov t0d, t8d  ; a
    sar t0d, 1
    sub t10d, t0d
    mov t0d, t9d  ; b
    imul t0d, t4d
    sub t10d, t0d
    BCASTW m_c, t10d

    BCASTW 0, t8d
%if a_shift
    psllw mm_van, m0, a_shift  ; a * (mmsize / 2)
%endif
    pmullw m0, mm_index
    psubw mm_c, m0  ; c - a * i

    mov t0d, t8d  ; a
    sar t0d, 31
    xor t8d, t0d
    sub t8d, t0d  ; abs_a
    mov t0d, t9d  ; b
    mov t10d, t9d
    sar t0d, 31
    xor t10d, t0d
    sub t10d, t0d  ; abs_b
%if ARCH_X86_64 == 0
    shl t10d, 16
    or t8d, t10d  ; abs_ab
%endif

    CALC_RES_ADDR %1, t4, t0
%if ARCH_X86_64
    CALC_RES_ADDR %1, t5, t0, skip
%endif
    cmp t4, up_addr
    jz .single_line

%if ARCH_X86_64 || a_shift == 0
    CALC_VBA %1, t9
%endif

    test t6d, t6d
    jz .generic_fist
    mov t2d, 64
    sub t2d, t6d  ; 64 - dn_pos
    add t6d, 64  ; 64 + dn_pos
    FILL_BORDER_LINE %1, t4,t8,t9,t10,t2,t6, t0,t1, 0,1,2,3,4,5

%if ARCH_X86_64 == 0
    mov t5, up_addr
%if a_shift
    CALC_VBA %1, t9
%endif
%endif

    psubw mm_c, mm_vba
    add t4, 2 << %1
    cmp t4, t5
    jge .end_loop
%if ARCH_X86_64 == 0
    jmp .bulk_fill
%endif

.generic_fist
%if ARCH_X86_64 == 0
    mov t5, up_addr
%if a_shift
    CALC_VBA %1, t9
%endif
%endif

.bulk_fill
    mov t2d, 1 << (13 - %1)
    mov t0d, t9d  ; b
    sar t0d, 1
    sub t2d, t0d  ; base
%if ARCH_X86_64
    mov t0d, t10d  ; abs_b
    cmp t0d, t8d  ; abs_a
    cmovg t0d, t8d
%else
    mov t0d, t8d  ; abs_ab
    shr t0d, 16  ; abs_b
    cmp t0w, t8w
    cmovg t0w, t8w
%endif
    add t0d, 2
    sar t0d, 2  ; dc
%if ARCH_X86_64
    sub t2d, t0d  ; base - dc
%else
    sub t2w, t0w  ; base - dc
%endif
    add t0d, t0d  ; 2 * dc
    BCASTW 2, t0d

%if ARCH_X86_64
    BCASTW 3, t2d
    paddw mm_c, m3
%else
    BCASTW 0, t2d
    paddw mm_c, m0

    mova mm_full, [words_tile%2]
%endif
.internal_loop
%assign i 0
%rep (2 << %1) / mmsize
%if i
    psubw mm_c, mm_van
%endif
    CALC_LINE %1, 0, m_c,2, m_zero,m_full, 1
    paddw m0, [t4 + i]
    mova [t4 + i], m0
%assign i i + mmsize
%endrep
    psubw mm_c, mm_vba
    add t4, 2 << %1
    cmp t4, t5
    jl .internal_loop
%if ARCH_X86_64
    psubw mm_c, m3
%else
    BCASTW 0, t2d
    psubw mm_c, m0
%endif

.end_loop
%if ARCH_X86_64
    test t7d, t7d
    jz .end_line_loop
    xor t6d, t6d
%else
    mov t2d, up_pos
    test t2d, t2d
    jz .end_line_loop
    mov t6d, t2d
    jmp .last_line
%endif

.single_line
%if ARCH_X86_64 == 0
    mov t7d, up_pos
%endif
    mov t2d, t7d
    sub t2d, t6d  ; up_pos - dn_pos
    add t6d, t7d  ; up_pos + dn_pos
.last_line
    FILL_BORDER_LINE %1, t4,t8,t9,t10,t2,t6, t0,t1, 0,1,2,3,4,5

.end_line_loop
%if ARCH_X86_64
    add r2, line_size
    sub r3, 1
%else
    sub dword r3m, 1
%endif
    jnz .line_loop

%if ARCH_X86_64 == 0
    mov r0, r0m
    mov r1, r1m
%endif
    GET_RES_ADDR r2
    lea r3, [rstk + delta_offs]
    SAVE_RESULT %1, r0,r1,r2,r3, r4,t2, 0,1,2,3
    ADD rstk, alloc_size
    RET
%endmacro

INIT_XMM sse2
FILL_GENERIC_TILE 4,16
FILL_GENERIC_TILE 5,32
INIT_YMM avx2
FILL_GENERIC_TILE 4,16
FILL_GENERIC_TILE 5,32

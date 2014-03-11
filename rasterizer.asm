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
; BROADCAST xmm_dst, r_src
;------------------------------------------------------------------------------

%macro BROADCAST 2
    movd %1, %2
    punpcklwd %1, %1
    pshufd %1, %1, 0
%endmacro

;------------------------------------------------------------------------------
; void fill_solid_tile16( uint8_t *buf, ptrdiff_t stride );
;------------------------------------------------------------------------------

INIT_XMM sse2
cglobal fill_solid_tile16, 2,2,1
    pcmpeqd xmm0, xmm0
%rep 15
    movaps [r0], xmm0
    add r0, r1
%endrep
    movaps [r0], xmm0
    RET

;------------------------------------------------------------------------------
; void fill_solid_tile32( uint8_t *buf, ptrdiff_t stride );
;------------------------------------------------------------------------------

INIT_XMM sse2
cglobal fill_solid_tile32, 2,2,1
    pcmpeqd xmm0, xmm0
%rep 31
    movaps [r0], xmm0
    movaps [r0 + 16], xmm0
    add r0, r1
%endrep
    movaps [r0], xmm0
    movaps [r0 + 16], xmm0
    RET

;------------------------------------------------------------------------------
; CALC_LINE tile_order, dst, src, delta, zero, full, tmp
;------------------------------------------------------------------------------

%macro CALC_LINE 7
    movaps xmm%2, %3
    movaps xmm%7, %3
    pmaxsw xmm%2, %5
    pminsw xmm%2, %6
    paddw xmm%7, %4
    pmaxsw xmm%7, %5
    pminsw xmm%7, %6
    paddw xmm%2, xmm%7
    psraw xmm%2, 7 - %1
%endmacro

;------------------------------------------------------------------------------
; FILL_HALFPLANE_TILE tile_order, suffix
; void fill_halfplane_tile%2( uint8_t *buf, ptrdiff_t stride,
;                             int32_t a, int32_t b, int64_t c, int32_t scale );
;------------------------------------------------------------------------------

%macro FILL_HALFPLANE_TILE 2
%if ARCH_X86_64
cglobal fill_halfplane_tile%2, 6,7,9
    movsxd r2q, r2d  ; a
    movsxd r3q, r3d  ; b
    sar r4q, 7 + %1  ; c >> (tile_order + 7)
    movsxd r5q, r5d  ; scale
    mov r6q, 1 << (45 + %1)
    imul r2q, r5q
    add r2q, r6q
    sar r2q, 46 + %1  ; aa
    imul r3q, r5q
    add r3q, r6q
    sar r3q, 46 + %1  ; bb
    imul r4q, r5q
    shr r6q, 1 + %1
    add r4q, r6q
    sar r4q, 45  ; cc
%else
cglobal fill_halfplane_tile%2, 0,7,8
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

    BROADCAST xmm1, r4d  ; cc
    BROADCAST xmm2, r2d  ; aa
    movdqa xmm3, xmm2
    pmullw xmm2, [words_index]
    psubw xmm1, xmm2  ; cc - aa * i
    psllw xmm3, 3  ; 8 * aa

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
    BROADCAST xmm2, r4d
    psubw xmm1, xmm2  ; c1 = cc - aa * i - delta
    paddw xmm2, xmm2  ; 2 * delta

    imul r2d, r2d, (1 << %1) - 8
    sub r3d, r2d  ; bb - (tile_size - 8) * aa
%if ARCH_X86_64
    BROADCAST xmm8, r3d
%else
    and r3d, 0xFFFF
    imul r3d, 0x10001
%endif

    pxor xmm0, xmm0
    movdqa xmm4, [words_tile%2]
    mov r2d, (1 << %1)
    jmp .loop_entry

.loop_start
    add r0, r1
%if ARCH_X86_64
    psubw xmm1, xmm8
%else
    movd xmm7, r3d
    pshufd xmm7, xmm7, 0
    psubw xmm1, xmm7
%endif
.loop_entry
%assign i 0
%rep (1 << %1) / 16
%if i > 0
    psubw xmm1, xmm3
%endif
    CALC_LINE %1, 5, xmm1,xmm2, xmm0,xmm4, 7
    psubw xmm1, xmm3
    CALC_LINE %1, 6, xmm1,xmm2, xmm0,xmm4, 7
    packuswb xmm5, xmm6
    movaps [r0 + i], xmm5
%assign i i + 16
%endrep
    sub r2d,1
    jnz .loop_start
    RET
%endmacro

INIT_XMM sse2
FILL_HALFPLANE_TILE 4,16
INIT_XMM sse2
FILL_HALFPLANE_TILE 5,32

;------------------------------------------------------------------------------
; struct Segment
; {
;     int32_t x_min, x_max, y_min, y_max;
;     int32_t a, b, scale, flags;
;     int64_t c;
; };
;------------------------------------------------------------------------------

%assign SEGOFFS_X_MIN  4 * 0
%assign SEGOFFS_X_MAX  4 * 1
%assign SEGOFFS_Y_MIN  4 * 2
%assign SEGOFFS_Y_MAX  4 * 3
%assign SEGOFFS_A      4 * 4
%assign SEGOFFS_B      4 * 5
%assign SEGOFFS_SCALE  4 * 6
%assign SEGOFFS_FLAGS  4 * 7
%assign SEGOFFS_C      4 * 8
%assign SIZEOF_SEGMENT 4 * 10

%assign SEGFLAG_UP           1 << 0
%assign SEGFLAG_UR_DL        1 << 1
%assign SEGFLAG_EXACT_LEFT   1 << 2
%assign SEGFLAG_EXACT_RIGHT  1 << 3
%assign SEGFLAG_EXACT_BOTTOM 1 << 4
%assign SEGFLAG_EXACT_TOP    1 << 5

;------------------------------------------------------------------------------
; ZEROFILL dst, size/16, tmp1
;------------------------------------------------------------------------------

%macro ZEROFILL 3
    mov r%3, (%2) / 8
%%zerofill_loop:
    movaps [r%1 + 0x00], xmm_zero
    movaps [r%1 + 0x10], xmm_zero
    movaps [r%1 + 0x20], xmm_zero
    movaps [r%1 + 0x30], xmm_zero
    movaps [r%1 + 0x40], xmm_zero
    movaps [r%1 + 0x50], xmm_zero
    movaps [r%1 + 0x60], xmm_zero
    movaps [r%1 + 0x70], xmm_zero
    add r%1, 0x80
    sub r%3, 1
    jnz %%zerofill_loop
%assign %%i 0
%rep (%2) & 7
    movaps [r%1 + %%i], xmm_zero
%assign %%i %%i + 16
%endrep
%endmacro

;------------------------------------------------------------------------------
; CALC_DELTA_FLAG res, line, tmp1, tmp2
;------------------------------------------------------------------------------

%macro CALC_DELTA_FLAG 4
    mov r%3d, [%2 + SEGOFFS_FLAGS]
    xor r%4d, r%4d
    cmp r%4d, [%2 + SEGOFFS_X_MIN]
    cmovz r%4d, r%3d
    xor r%1d, r%1d
    test r%3d, SEGFLAG_UR_DL
    cmovnz r%1d, r%4d
    shl r%3d, 2
    xor r%1d, r%3d
    and r%4d, 4
    and r%1d, 4
    lea r%1d, [r%1d + 2 * r%1d]
    xor r%1d, r%4d  ; bit 3 - dn_delta, bit 2 - up_delta
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
    %error "up/dn expected!"
%endif

    test r%3d, %%flag
    jz %%skip
    lea r%5d, [4 * r%4d - 256]
    %%opi [%2], r%5w
    lea r%5d, [4 * r%4d]
    %%op [%2 + 2], r%5w
%%skip:
%endmacro

;------------------------------------------------------------------------------
; CALC_VBA tile_order
;------------------------------------------------------------------------------

%macro CALC_VBA 1
    BROADCAST xmm_vba, r_b
%rep (1 << %1) / 8 - 1
    psubw xmm_vba, xmm_va8  ; b - (tile_size - 8) * a
%endrep
%endmacro

;------------------------------------------------------------------------------
; FILL_BORDER_LINE tile_order, res, size, sum,
;                  tmp5, tmp6, xmm7, xmm8, xmm9, xmm10, xmm11, [xmm12]
;------------------------------------------------------------------------------

%macro FILL_BORDER_LINE 12
    mov r%5d, r%3d
    shl r%5d, 8 - %1  ; size << (8 - tile_order)
    xor r%6d, r%6d
%if ARCH_X86_64
    sub r%5d, r_abs_a
    cmovg r%5d, r%6d
    add r%5d, 1 << (14 - %1)
    shl r%5d, 2 * %1 - 5  ; w
    BROADCAST xmm%12, r%5d

    mov r%6d, r_abs_b
    imul r%6d, r%3d
    sar r%6d, 6  ; dc_b
    cmp r%6d, r_abs_a
    cmovg r%6d, r_abs_a
%else
    sub r%5w, r_abs_a
    cmovg r%5d, r%6d
    add r%5w, 1 << (14 - %1)
    shl r%5d, 2 * %1 - 5  ; w

    mov r%6d, r_abs_ab
    shr r%6d, 16  ; abs_b
    imul r%6d, r%3d
    sar r%6d, 6  ; dc_b
    cmp r%6w, r_abs_a
    cmovg r%6w, r_abs_a
%endif
    add r%6d, 2
    sar r%6d, 2  ; dc

    imul r%4d, r_b  ; b * sum
    sar r%4d, 7  ; b * avg
    add r%4d, r%6d  ; b * avg + dc
    add r%6d, r%6d  ; 2 * dc

    imul r%4d, r%5d
    sar r%4d, 16
    sub r%4d, r%3d  ; -offs1
    BROADCAST xmm%7, r%4d
    imul r%6d, r%5d
    sar r%6d, 16  ; offs2 - offs1
    BROADCAST xmm%8, r%6d
    add r%3d, r%3d
    BROADCAST xmm%9, r%3d
%if ARCH_X86_64 == 0
    imul r%5d, 0x10001
%endif

%assign %%i 0
%rep (1 << %1) / 8
%if %%i
    psubw xmm_c, xmm_va8
%endif
    movaps xmm%10, xmm_c
%if ARCH_X86_64
    pmulhw xmm%10, xmm%12
%else
    movd xmm%11, r%5d
    pshufd xmm%11, xmm%11, 0
    pmulhw xmm%10, xmm%11
%endif
    psubw xmm%10, xmm%7  ; c1
    movaps xmm%11, xmm%10
    paddw xmm%11, xmm%8  ; c2
    pmaxsw xmm%10, xmm_zero
    pminsw xmm%10, xmm%9
    pmaxsw xmm%11, xmm_zero
    pminsw xmm%11, xmm%9
    paddw xmm%10, xmm%11
    movaps xmm%11, [%2 + %%i]
    paddw xmm%10, xmm%11
    movaps [%2 + %%i], xmm%10
%assign %%i %%i + 16
%endrep
%endmacro

;------------------------------------------------------------------------------
; SAVE_RESULT tile_order, buf, stride, src, delta,
;             tmp6, tmp7, xmm8, xmm9, xmm10, xmm11
;------------------------------------------------------------------------------

%macro SAVE_RESULT 11
    mov r%6d, 1 << %1
    xor r%7d, r%7d
%%save_loop:
    add r%7w, [r%5]
    BROADCAST xmm%10, r%7d
    add r%5, 2

%assign %%i 0
%rep (1 << %1) / 16
    movaps xmm%8, [r%4 + 2 * %%i]
    paddw xmm%8, xmm%10
    pxor xmm%11, xmm%11
    psubw xmm%11, xmm%8
    pmaxsw xmm%8, xmm%11
    movaps xmm%9, [r%4 + 2 * %%i + 16]
    paddw xmm%9, xmm%10
    pxor xmm%11, xmm%11
    psubw xmm%11, xmm%9
    pmaxsw xmm%9, xmm%11
    packuswb xmm%8, xmm%9
    movaps [r%2 + %%i], xmm%8
%assign %%i %%i + 16
%endrep

    add r%2, r%3
    add r%4, 2 << %1
    sub r%6d, 1
    jnz %%save_loop
%endmacro

;------------------------------------------------------------------------------
; DEF_REG_NAME name, index
;------------------------------------------------------------------------------

%macro DEFINE_INDIRECT 2
    %define %1 %2
%endmacro

%macro DEF_REG_NAME 2
    DEFINE_INDIRECT r_ %+ %1,       r %+ %2 %+ d
    DEFINE_INDIRECT r_ %+ %1 %+ _,  r %+ %2
    DEFINE_INDIRECT r_ %+ %1 %+ _w, r %+ %2 %+ w
    DEFINE_INDIRECT r_ %+ %1 %+ _d, r %+ %2 %+ d
    DEFINE_INDIRECT r_ %+ %1 %+ _q, r %+ %2 %+ q
%endmacro

;------------------------------------------------------------------------------
; GET_RES_ADDR dst
; CALC_RES_ADDR tile_order, dst/index, tmp, [skip_calc]
;------------------------------------------------------------------------------

%macro GET_RES_ADDR 1
%if HAVE_ALIGNED_STACK
    mov r%1, rstk
%else
    lea r%1, [rstk + 15]
    and r%1, ~15
%endif
%endmacro

%macro CALC_RES_ADDR 3-4 noskip
    shl r%2d, 1 + %1
%if HAVE_ALIGNED_STACK
    add r%2, rstk
%else
%ifidn %4, noskip
    lea r%3, [rstk + 15]
    and r%3, ~15
%endif
    add r%2, r%3
%endif
%endmacro

;------------------------------------------------------------------------------
; FILL_GENERIC_TILE tile_order, suffix
; void fill_generic_tile%2( uint8_t *buf, ptrdiff_t stride,
;                           const struct Segment *line, size_t n_lines,
;                           int winding )
;------------------------------------------------------------------------------

%macro FILL_GENERIC_TILE 2
%if ARCH_X86_64
cglobal fill_generic_tile%2, 5,14,12
    %define xmm_zero  xmm6
    %define xmm_full  xmm7
    %define xmm_index xmm8
    %define xmm_c     xmm9
    %define xmm_vba   xmm10
    %define xmm_va8   xmm11

    %assign alloc_size 0
%else
cglobal fill_generic_tile%2, 0,7,8
    %define xmm_zero  xmm5
    %define xmm_va8   xmm6
    %define xmm_c     xmm7

    %define xmm_index [words_index]
    %define xmm_vba   xmm3
    %define xmm_full  xmm4

    %assign alloc_size 8
%endif
    %assign tile_size 1 << %1
    %assign delta_offs 2 * tile_size * tile_size
    %assign alloc_size 2 * tile_size * (tile_size + 1) + 4 + alloc_size
    %assign n tile_size * (tile_size + 1) / 8
%if HAVE_ALIGNED_STACK
    %assign alloc_size ((alloc_size + stack_offset + gprsize + 15) & ~15) - stack_offset - gprsize
%else
    %assign alloc_size alloc_size + 32
    %assign delta_offs delta_offs + 16
    %assign n n + 1
%endif
    SUB rstk, alloc_size

    GET_RES_ADDR 6
    pxor xmm_zero, xmm_zero
    ZEROFILL 6, n, 5

%if ARCH_X86_64 == 0
    mov r4d, r4m
%endif
    shl r4d, 8
    mov [rstk + delta_offs], r4w

%if ARCH_X86_64
    movdqa xmm_index, [words_index]
    movdqa xmm_full, [words_tile%2]
    DEF_REG_NAME tmp0, 10
    DEF_REG_NAME tmp1, 11
%else
    %define up_addr [rstk + delta_offs + 2 * tile_size + 4]
    %define up_pos [rstk + delta_offs + 2 * tile_size + 8]
    DEF_REG_NAME tmp0, 0
    DEF_REG_NAME tmp1, 1
%endif

.line_loop
%if ARCH_X86_64
    %define r_line r2
%else
    %define r_line r3
    mov r_line, r2m
    lea r_tmp0_, [r_line + SIZEOF_SEGMENT]
    mov r2m, r_tmp0_
%endif
    CALC_DELTA_FLAG _tmp0_, r_line, 4,5

    mov r4d, [r_line + SEGOFFS_Y_MIN]
    mov r5d, [r_line + SEGOFFS_Y_MAX]
%if ARCH_X86_64
    mov r8d, r4d
    mov r6d, r4d
    and r6d, 63  ; dn_pos
    shr r4d, 6  ; dn
    mov r9d, r5d
    mov r7d, r5d
    and r7d, 63  ; up_pos
    shr r9d, 6  ; up

    UPDATE_DELTA dn, rstk + 2 * r4 + delta_offs, _tmp0_,6, _tmp1_
    UPDATE_DELTA up, rstk + 2 * r9 + delta_offs, _tmp0_,7, _tmp1_
    cmp r8d, r5d
%else
    lea r1d, [r_tmp0 + 1]
    cmp r4d, r5d
    cmovnz r_tmp0, r1d  ; bit 0 -- not horz line

    mov r6d, r5d
    and r6d, 63  ; up_pos
    shr r5d, 6  ; up
    UPDATE_DELTA up, rstk + 2 * r5 + delta_offs, _tmp0_,6, _tmp1_

    CALC_RES_ADDR %1, 5, _tmp1_
    mov up_addr, r5
    mov up_pos, r6d

    mov r6d, r4d
    and r6d, 63  ; dn_pos
    shr r4d, 6  ; dn
    UPDATE_DELTA dn, rstk + 2 * r4 + delta_offs, _tmp0_,6, _tmp1_
    test r_tmp0, 1
%endif
    jz .end_line_loop

%if ARCH_X86_64
    DEF_REG_NAME a, 8
    DEF_REG_NAME b, 12
    DEF_REG_NAME c, 13

    movsxd r_a_q, dword [r2 + SEGOFFS_A]
    movsxd r_b_q, dword [r2 + SEGOFFS_B]
    mov r_c_q, [r2 + SEGOFFS_C]
    sar r_c_q, 7 + %1  ; c >> (tile_order + 7)
    movsxd r_tmp0_q, dword [r2 + SEGOFFS_SCALE]
    mov r_tmp1_q, 1 << (45 + %1)
    imul r_a_q, r_tmp0_q
    add r_a_q, r_tmp1_q
    sar r_a_q, 46 + %1
    imul r_b_q, r_tmp0_q
    add r_b_q, r_tmp1_q
    sar r_b_q, 46 + %1
    imul r_c_q, r_tmp0_q
    shr r_tmp1_q, 1 + %1
    add r_c_q, r_tmp1_q
    sar r_c_q, 45
%else
    DEF_REG_NAME a, 2
    DEF_REG_NAME b, 3
    DEF_REG_NAME c, 5

    mov r0d, [r3 + SEGOFFS_C]
    mov r2d, [r3 + SEGOFFS_C + 4]
    mov r1d, [r3 + SEGOFFS_SCALE]
    shr r0d, 7 + %1
    shl r2d, 25 - %1
    or r0d, r2d  ; r0d (eax) = c >> (tile_order + 7)
    imul r1d  ; r2d (edx) = (c >> ...) * scale >> 32
    add r2d, 1 << 12
    sar r2d, 13
    mov r_c, r2d
    mov r0d, [r3 + SEGOFFS_B]  ; r0d (eax)
    imul r1d  ; r2d (edx) = b * scale >> 32
    add r2d, 1 << (13 + %1)
    sar r2d, 14 + %1
    mov r0d, [r3 + SEGOFFS_A]  ; r0d (eax)
    mov r_b, r2d
    imul r1d  ; r_a (edx) = a * scale >> 32
    add r_a, 1 << (13 + %1)
    sar r_a, 14 + %1
%endif

    mov r_tmp0, r_a
    sar r_tmp0, 1
    sub r_c, r_tmp0
    mov r_tmp0, r_b
    imul r_tmp0, r4d
    sub r_c, r_tmp0
    BROADCAST xmm_c, r_c

    BROADCAST xmm0, r_a
    movdqa xmm_va8, xmm0
    pmullw xmm0, xmm_index
    psubw xmm_c, xmm0  ; c - a * i
    psllw xmm_va8, 3  ; 8 * a

%if ARCH_X86_64
    %define r_abs_a r_a
    %define r_abs_b r_c
%else
    %define r_abs_a r_a_w
    %define r_abs_ab r_a
    %define r_abs_b r_tmp1
%endif

    mov r_tmp0, r_a
    sar r_tmp0, 31
    xor r_a, r_tmp0
    sub r_a, r_tmp0  ; abs_a
    mov r_tmp0, r_b
    mov r_abs_b, r_b
    sar r_tmp0, 31
    xor r_abs_b, r_tmp0
    sub r_abs_b, r_tmp0
%if ARCH_X86_64 == 0
    shl r_abs_b, 16
    or r_abs_ab, r_abs_b
%endif

    CALC_RES_ADDR %1, 4, _tmp0_
%if ARCH_X86_64
    CALC_RES_ADDR %1, 9, _tmp0_, skip
    %define up_addr r9
    %define r_up r9
%endif
    cmp r4, up_addr
    jz .single_line

%if ARCH_X86_64
    CALC_VBA %1
%endif

    test r6d, r6d
    jz .generic_fist
    mov r5d, 64
    sub r5d, r6d  ; 64 - dn_pos
    add r6d, 64  ; 64 + dn_pos
    FILL_BORDER_LINE %1, r4,5,6, _tmp0_,_tmp1_, 0,1,2,3,4,5

%if ARCH_X86_64 == 0
    %define r_up r6
    mov r_up, up_addr
    CALC_VBA %1
%endif

    psubw xmm_c, xmm_vba
    add r4, 2 << %1
    cmp r4, r_up
    jge .end_loop
%if ARCH_X86_64 == 0
    jmp .bulk_fill
%endif

.generic_fist
%if ARCH_X86_64 == 0
    mov r_up, up_addr
    CALC_VBA %1
%endif

.bulk_fill
    mov r5d, 1 << (13 - %1)
    mov r_tmp0, r_b
    sar r_tmp0, 1
    sub r5d, r_tmp0  ; base
%if ARCH_X86_64
    mov r_tmp0, r_abs_b
    cmp r_tmp0, r_abs_a
    cmovg r_tmp0, r_abs_a
%else
    mov r_tmp0, r_abs_ab
    shr r_tmp0, 16  ; abs_b
    cmp r_tmp0_w, r_abs_a
    cmovg r_tmp0_w, r_abs_a
%endif
    add r_tmp0, 2
    sar r_tmp0, 2  ; dc
%if ARCH_X86_64
    sub r5d, r_tmp0  ; base - dc
%else
    sub r5w, r_tmp0_w  ; base - dc
%endif
    add r_tmp0, r_tmp0  ; 2 * dc
    BROADCAST xmm2, r_tmp0

%if ARCH_X86_64
    BROADCAST xmm3, r5d
    paddw xmm_c, xmm3
%else
    imul r5d, 0x10001
    movd xmm0, r5d
    pshufd xmm0, xmm0, 0
    paddw xmm_c, xmm0

    movdqa xmm_full, [words_tile%2]
%endif
.internal_loop
%assign i 0
%rep (1 << %1) / 8
%if i
    psubw xmm_c, xmm_va8
%endif
    CALC_LINE %1, 0, xmm_c,xmm2, xmm_zero,xmm_full, 1
    movaps xmm1, [r4 + i]
    paddw xmm0, xmm1
    movaps [r4 + i], xmm0
%assign i i + 16
%endrep
    psubw xmm_c, xmm_vba
    add r4, 2 << %1
    cmp r4, r_up
    jl .internal_loop
%if ARCH_X86_64
    psubw xmm_c, xmm3
%else
    movd xmm0, r5d
    pshufd xmm0, xmm0, 0
    psubw xmm_c, xmm0
%endif

.end_loop
%if ARCH_X86_64
    %define r_pos r7d
    test r_pos, r_pos
    jz .end_line_loop
    xor r6d, r6d
%else
    mov r5d, up_pos
    test r5d, r5d
    jz .end_line_loop
    mov r6d, r5d
    jmp .last_line
%endif

.single_line
%if ARCH_X86_64 == 0
    %define r_pos r0d
    mov r_pos, up_pos
%endif
    mov r5d, r_pos
    sub r5d, r6d  ; up_pos - dn_pos
    add r6d, r_pos  ; up_pos + dn_pos
.last_line
    FILL_BORDER_LINE %1, r4,5,6, _tmp0_,_tmp1_, 0,1,2,3,4,5

.end_line_loop
%if ARCH_X86_64
    add r2, SIZEOF_SEGMENT
    sub r3, 1
%else
    sub dword r3m, 1
%endif
    jnz .line_loop

%if ARCH_X86_64 == 0
    mov r0, r0m
    mov r1, r1m
%endif
    GET_RES_ADDR 2
    lea r3, [rstk + delta_offs]
    SAVE_RESULT %1, 0,1,2,3, 4,5, 0,1,2,3
    ADD rstk, alloc_size
    RET
%endmacro

INIT_XMM sse2
FILL_GENERIC_TILE 4,16
INIT_XMM sse2
FILL_GENERIC_TILE 5,32

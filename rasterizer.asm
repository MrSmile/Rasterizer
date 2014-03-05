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

%define HAVE_ALIGNED_STACK 1
%include "x86inc.asm"

SECTION_RODATA 32

bcast_word: db 0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1
words_index: dw 0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0A,0x0B,0x0C,0x0D,0x0E,0x0F
words_1023: dw 1023,1023,1023,1023,1023,1023,1023,1023,1023,1023,1023,1023,1023,1023,1023,1023
words_511: dw 511,511,511,511,511,511,511,511,511,511,511,511,511,511,511,511

SECTION .text

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


; src, delta, zero, full, dst, tmp, shift
%macro CALC_LINE 7
    movaps xmm%5, xmm%1
    movaps xmm%6, xmm%5
    pmaxsw xmm%5, xmm%3
    pminsw xmm%5, xmm%4
    paddw xmm%6, xmm%2
    pmaxsw xmm%6, xmm%3
    pminsw xmm%6, xmm%4
    paddw xmm%5, xmm%6
    psraw xmm%5, %7
%endmacro

;------------------------------------------------------------------------------
; void fill_halfplane_tile16( uint8_t *buf, ptrdiff_t stride,
;                             int32_t a, int32_t b, int64_t c, int32_t scale );
;------------------------------------------------------------------------------

cglobal fill_halfplane_tile16, 6,7,9
    movsxd r2, r2d  ; a
    movsxd r3, r3d  ; b
    sar r4, 11  ; c >> 11
    movsxd r5, r5d  ; scale
    mov r6, 1 << 49
    imul r2, r5
    add r2, r6
    sar r2, 50  ; aa
    imul r3, r5
    add r3, r6
    sar r3, 50  ; bb
    imul r4, r5
    shr r6, 5
    add r4, r6
    sar r4, 45  ; cc
    add r4d, 1 << 9
    mov r6d, r2d
    add r6d, r3d
    sar r6d, 1
    sub r4d, r6d

    movdqa xmm0, [bcast_word wrt rip]
    movd xmm1, r4d  ; cc
    pshufb xmm1, xmm0  ; SSSE3
    movd xmm2, r2d  ; aa
    pshufb xmm2, xmm0  ; SSSE3
    movdqa xmm3, xmm2
    pmullw xmm2, [words_index wrt rip]
    psubw xmm1, xmm2  ; cc - aa * i
    psllw xmm3, 3  ; 8 * aa

    mov r4d, r2d
    mov r6d, r4d
    sar r6d, 31
    xor r4d, r6d
    sub r4d, r6d  ; abs_a
    mov r5d, r3d
    mov r6d, r5d
    sar r6d, 31
    xor r5d, r6d
    sub r5d, r6d  ; abs_b
    cmp r4d, r5d
    cmovg r4d, r5d
    add r4d, 2
    sar r4d, 2  ; delta
    movd xmm2, r4d
    pshufb xmm2, xmm0  ; SSSE3
    psubw xmm1, xmm2  ; c1 = cc - aa * i - delta
    paddw xmm2, xmm2  ; 2 * delta

    shl r2d, 3
    sub r3d, r2d  ; bb - 8 * aa
    movd xmm8, r3d
    pshufb xmm8, xmm0  ; SSSE3

    pxor xmm0, xmm0
    movdqa xmm4, [words_1023 wrt rip]

    %define FIRST
    %rep 16
        %ifdef FIRST
            %undef FIRST
        %else
            add r0, r1
            psubw xmm1, xmm8
        %endif

        CALC_LINE 1, 2, 0, 4, 5, 7, 3
        psubw xmm1, xmm3
        CALC_LINE 1, 2, 0, 4, 6, 7, 3
        packuswb xmm5, xmm6
        movaps [r0], xmm5
    %endrep
    RET


;------------------------------------------------------------------------------
; void fill_halfplane_tile32( uint8_t *buf, ptrdiff_t stride,
;                             int32_t a, int32_t b, int64_t c, int32_t scale );
;------------------------------------------------------------------------------

cglobal fill_halfplane_tile32, 6,7,9
    movsxd r2, r2d  ; a
    movsxd r3, r3d  ; b
    sar r4, 12  ; c >> 12
    movsxd r5, r5d  ; scale
    mov r6, 1 << 50
    imul r2, r5
    add r2, r6
    sar r2, 51  ; aa
    imul r3, r5
    add r3, r6
    sar r3, 51  ; bb
    imul r4, r5
    shr r6, 6
    add r4, r6
    sar r4, 45  ; cc
    add r4d, 1 << 8
    mov r6d, r2d
    add r6d, r3d
    sar r6d, 1
    sub r4d, r6d

    movdqa xmm0, [bcast_word wrt rip]
    movd xmm1, r4d  ; cc
    pshufb xmm1, xmm0  ; SSSE3
    movd xmm2, r2d  ; aa
    pshufb xmm2, xmm0  ; SSSE3
    movdqa xmm3, xmm2
    pmullw xmm2, [words_index wrt rip]
    psubw xmm1, xmm2  ; cc - aa * i
    psllw xmm3, 3  ; 8 * aa

    mov r4d, r2d
    mov r6d, r4d
    sar r6d, 31
    xor r4d, r6d
    sub r4d, r6d  ; abs_a
    mov r5d, r3d
    mov r6d, r5d
    sar r6d, 31
    xor r5d, r6d
    sub r5d, r6d  ; abs_b
    cmp r4d, r5d
    cmovg r4d, r5d
    add r4d, 2
    sar r4d, 2  ; delta
    movd xmm2, r4d
    pshufb xmm2, xmm0  ; SSSE3
    psubw xmm1, xmm2  ; c1 = cc - aa * i - delta
    paddw xmm2, xmm2  ; 2 * delta

    imul r2d, r2d, 24
    sub r3d, r2d  ; bb - 24 * aa
    movd xmm8, r3d
    pshufb xmm8, xmm0  ; SSSE3

    pxor xmm0, xmm0
    movdqa xmm4, [words_511 wrt rip]

    %define FIRST
    %rep 32
        %ifdef FIRST
            %undef FIRST
        %else
            add r0, r1
            psubw xmm1, xmm8
        %endif

        CALC_LINE 1, 2, 0, 4, 5, 7, 2
        psubw xmm1, xmm3
        CALC_LINE 1, 2, 0, 4, 6, 7, 2
        packuswb xmm5, xmm6
        movaps [r0], xmm5
        psubw xmm1, xmm3
        CALC_LINE 1, 2, 0, 4, 5, 7, 2
        psubw xmm1, xmm3
        CALC_LINE 1, 2, 0, 4, 6, 7, 2
        packuswb xmm5, xmm6
        movaps [r0 + 16], xmm5
    %endrep
    RET


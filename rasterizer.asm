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
words_tile16: dw 1024,1024,1024,1024,1024,1024,1024,1024,1024,1024,1024,1024,1024,1024,1024,1024
words_tile32: dw 512,512,512,512,512,512,512,512,512,512,512,512,512,512,512,512

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


;------------------------------------------------------------------------------
; CALC_LINE src, delta, zero, full, dst, tmp, shift
;------------------------------------------------------------------------------
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
            shl r5q, 1 + %1
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

        movdqa xmm0, [bcast_word]
        movd xmm1, r4d  ; cc
        pshufb xmm1, xmm0  ; SSSE3
        movd xmm2, r2d  ; aa
        pshufb xmm2, xmm0  ; SSSE3
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
        movd xmm2, r4d
        pshufb xmm2, xmm0  ; SSSE3
        psubw xmm1, xmm2  ; c1 = cc - aa * i - delta
        paddw xmm2, xmm2  ; 2 * delta

        imul r2d, r2d, (1 << %1) - 8
        sub r3d, r2d  ; bb - (tile_size - 8) * aa
        %if ARCH_X86_64
            movd xmm8, r3d
            pshufb xmm8, xmm0  ; SSSE3
        %else
            and r3d, 0xFFFF
            mov r2d, r3d
            shl r2d, 16
            or r2d, r3d
        %endif

        pxor xmm0, xmm0
        movdqa xmm4, [words_tile%2]
        mov r3d, (1 << %1)
        jmp .loop_entry

        .loop_start
            add r0, r1
            %if ARCH_X86_64
                psubw xmm1, xmm8
            %else
                movd xmm7, r2d
                pshufd xmm7, xmm7, 0
                psubw xmm1, xmm7
            %endif
        .loop_entry
            %assign i 0
            %rep (1 << %1) / 16
                %if i > 0
                    psubw xmm1, xmm3
                %endif
                CALC_LINE 1, 2, 0, 4, 5, 7, 7 - %1
                psubw xmm1, xmm3
                CALC_LINE 1, 2, 0, 4, 6, 7, 7 - %1
                packuswb xmm5, xmm6
                movaps [r0 + i], xmm5
                %assign i i + 16
            %endrep
            sub r3d,1
            jnz .loop_start
        RET
%endmacro

INIT_XMM sse2
FILL_HALFPLANE_TILE 4,16
INIT_XMM sse2
FILL_HALFPLANE_TILE 5,32

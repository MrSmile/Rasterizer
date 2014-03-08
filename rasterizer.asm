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
; CALC_LINE dst, src, delta, zero, full, tmp, tile_order
;------------------------------------------------------------------------------

%macro CALC_LINE 7
    movaps xmm%1, %2
    movaps xmm%6, %2
    pmaxsw xmm%1, %4
    pminsw xmm%1, %5
    paddw xmm%6, %3
    pmaxsw xmm%6, %4
    pminsw xmm%6, %5
    paddw xmm%1, xmm%6
    psraw xmm%1, 7 - %7
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
                CALC_LINE 5, xmm1,xmm2, xmm0,xmm4, 7, %1
                psubw xmm1, xmm3
                CALC_LINE 6, xmm1,xmm2, xmm0,xmm4, 7, %1
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
; FILL_BORDER_LINE tile_order, res, size, sum,
;                  tmp5, tmp6, xmm7, xmm8, xmm9, xmm10, xmm11, xmm12
;------------------------------------------------------------------------------

%macro FILL_BORDER_LINE 12
    mov r%5d, r%3d
    shl r%5d, 8 - %1  ; size << (8 - tile_order)
    xor r%6d, r%6d
    sub r%5d, r_abs_a
    cmovg r%5d, r%6d
    add r%5d, 1 << (14 - %1)
    shl r%5d, 2 * %1 - 5  ; w
    movd xmm%7, r%5d
    pshufb xmm%7, xmm_bcast  ; SSSE3

    mov r%6d, r_abs_b
    imul r%6d, r%3d
    sar r%6d, 6  ; dc_b
    cmp r%6d, r_abs_a
    cmovg r%6d, r_abs_a
    add r%6d, 2
    sar r%6d, 2  ; dc

    imul r%4d, r_b  ; b * sum
    sar r%4d, 7  ; b * avg
    add r%4d, r%6d  ; b * avg + dc
    add r%6d, r%6d  ; 2 * dc

    imul r%4d, r%5d
    sar r%4d, 16
    sub r%4d, r%3d  ; -offs1
    movd xmm%8, r%4d
    pshufb xmm%8, xmm_bcast  ; SSSE3
    imul r%6d, r%5d
    sar r%6d, 16  ; offs2 - offs1
    movd xmm%9, r%6d
    pshufb xmm%9, xmm_bcast  ; SSSE3
    add r%3d, r%3d
    movd xmm%10, r%3d
    pshufb xmm%10, xmm_bcast  ; SSSE3

    %assign i 0
    %rep (1 << %1) / 8
        %if i
            psubw xmm_c, xmm_va8
        %endif
        movaps xmm%11, xmm_c
        pmulhw xmm%11, xmm%7
        psubw xmm%11, xmm%8  ; c1
        movaps xmm%12, xmm%11
        paddw xmm%12, xmm%9  ; c2
        pmaxsw xmm%11, xmm_zero
        pminsw xmm%11, xmm%10
        pmaxsw xmm%12, xmm_zero
        pminsw xmm%12, xmm%10
        paddw xmm%11, xmm%12
        movaps xmm%12, [%2 + i]
        paddw xmm%11, xmm%12
        movaps [%2 + i], xmm%11
        %assign i i + 16
    %endrep
%endmacro

;------------------------------------------------------------------------------
; FILL_GENERIC_TILE tile_order, suffix
; void fill_generic_tile%2( uint8_t *buf, ptrdiff_t stride,
;                           const struct Segment *line, size_t n_lines,
;                           int winding )
;------------------------------------------------------------------------------

%macro FILL_GENERIC_TILE 2
    %assign tile_size 1 << %1
    %define xmm_zero  xmm0
    %define xmm_full  xmm1
    %define xmm_bcast xmm2
    %define xmm_index xmm3
    %define xmm_va8   xmm4
    %define xmm_vba   xmm5
    %define xmm_c     xmm6

    cglobal fill_generic_tile%2, 5,15,13, 2 * tile_size * (tile_size + 1) + 16
        pxor xmm_zero, xmm_zero
        mov r6, rsp
        %assign n tile_size * (tile_size + 1) / 8
        mov r5d, n / 8
        .zerofill_loop
            movaps [r6 + 0x00], xmm_zero
            movaps [r6 + 0x10], xmm_zero
            movaps [r6 + 0x20], xmm_zero
            movaps [r6 + 0x30], xmm_zero
            movaps [r6 + 0x40], xmm_zero
            movaps [r6 + 0x50], xmm_zero
            movaps [r6 + 0x60], xmm_zero
            movaps [r6 + 0x70], xmm_zero
            add r6, 0x80
            sub r5d, 1
            jnz .zerofill_loop
        %assign i 0
        %rep n & 7
            movaps [r6 + (i & 0x7F)], xmm_zero
            %assign i i + 16
        %endrep
        shl r4d, 8
        %assign DELTA_OFFS 2 * tile_size * tile_size
        mov [rsp + DELTA_OFFS], r4w

        movdqa xmm_bcast, [bcast_word]
        movdqa xmm_index, [words_index]
        movdqa xmm_full, [words_tile%2]

        .line_loop
            mov r4d, [r2 + SEGOFFS_FLAGS]
            xor r5d, r5d
            cmp r5d, [r2 + SEGOFFS_X_MIN]
            cmovz r5d, r4d
            xor r10d, r10d
            test r4d, SEGFLAG_UR_DL
            cmovnz r10d, r5d
            shl r4d, 2
            xor r10d, r4d
            and r5d, 4
            and r10d, 4
            lea r10d, [r10d + 2 * r10d]
            xor r10d, r5d  ; bit 3 - dn_delta, bit 2 - up_delta

            mov r4d, [r2 + SEGOFFS_Y_MIN]
            mov r5d, [r2 + SEGOFFS_Y_MAX]
            mov r6d, r4d
            mov r8d, r4d
            and r6d, 63  ; dn_pos
            shr r8d, 6  ; dn
            mov r7d, r5d
            mov r9d, r5d
            and r7d, 63  ; up_pos
            shr r9d, 6  ; up

            test r10d, 1 << 3
            jz .no_delta_dn
            lea r11d, [4 * r6d]
            mov r12d, 256
            sub r12d, r11d
            sub [rsp + 2 * r8 + DELTA_OFFS], r12w
            sub [rsp + 2 * r8 + DELTA_OFFS + 2], r11w
        .no_delta_dn

            test r10d, 1 << 2
            jz .no_delta_up
            lea r11d, [4 * r7d]
            mov r12d, 256
            sub r12d, r11d
            add [rsp + 2 * r9 + DELTA_OFFS], r12w
            add [rsp + 2 * r9 + DELTA_OFFS + 2], r11w
        .no_delta_up

            cmp r4d, r5d
            jz .end_line_loop

            movsxd r11q, dword [r2 + SEGOFFS_A]
            movsxd r12q, dword [r2 + SEGOFFS_B]
            mov r13q, [r2 + SEGOFFS_C]
            sar r13q, 7 + %1  ; c >> (tile_order + 7)
            movsxd r10q, dword [r2 + SEGOFFS_SCALE]
            mov r14q, 1 << (45 + %1)
            imul r11q, r10q
            add r11q, r14q
            sar r11q, 46 + %1  ; a
            imul r12q, r10q
            add r12q, r14q
            sar r12q, 46 + %1  ; b
            imul r13q, r10q
            shr r14q, 1 + %1
            add r13q, r14q
            sar r13q, 45  ; c

            mov r10d, r11d
            sar r10d, 1
            sub r13d, r10d
            mov r10d, r12d
            imul r10d, r8d
            sub r13d, r10d ; c
            movd xmm_c, r13d
            pshufb xmm_c, xmm_bcast  ; SSSE3

            movd xmm7, r11d  ; a
            pshufb xmm7, xmm_bcast  ; SSSE3
            movdqa xmm_va8, xmm7
            pmullw xmm7, xmm_index
            psubw xmm_c, xmm7  ; c - a * i
            psllw xmm_va8, 3  ; 8 * a

            mov r10d, r11d
            sar r10d, 31
            xor r11d, r10d
            sub r11d, r10d  ; abs_a
            mov r14d, r12d  ; b
            mov r10d, r14d
            sar r10d, 31
            xor r14d, r10d
            sub r14d, r10d  ; abs_b

            %define r_abs_a r11d
            %define r_b r12d
            %define r_abs_b r14d
            shl r8d, 1 + %1
            add r8, rsp
            shl r9d, 1 + %1
            add r9, rsp
            cmp r8, r9
            jz .single_line

            mov r10d, 64
            sub r10d, r6d  ; 64 - dn_pos
            add r6d, 64  ; 64 + dn_pos
            FILL_BORDER_LINE %1, r8, 10,6, 4,5, 7,8,9,10,11,12

            movd xmm_vba, r_b
            pshufb xmm_vba, xmm_bcast  ; SSSE3
            %rep (1 << %1) / 8 - 1
                psubw xmm_vba, xmm_va8  ; b - (tile_size - 8) * a
            %endrep

            add r8, 2 << %1
            cmp r8, r9
            jge .end_loop

            mov r4d, 1 << (13 - %1)
            mov r10d, r_b
            sar r10d, 1
            sub r4d, r10d  ; base
            mov r5d, r_abs_b
            cmp r5d, r_abs_a
            cmovg r5d, r_abs_a
            add r5d, 2
            sar r5d, 2  ; dc
            sub r4d, r5d  ; base - dc
            add r5d, r5d  ; 2 * dc
            movd xmm7, r4d
            pshufb xmm7, xmm_bcast  ; SSSE3
            movd xmm8, r5d
            pshufb xmm8, xmm_bcast  ; SSSE3

            paddw xmm_c, xmm7
            .internal_loop
                psubw xmm_c, xmm_vba
                %assign i 0
                %rep (1 << %1) / 8
                    %if i
                        psubw xmm_c, xmm_va8
                    %endif
                    CALC_LINE 9, xmm_c,xmm8, xmm_zero,xmm_full, 10, %1
                    movaps xmm10, [r8 + i]
                    paddw xmm9, xmm10
                    movaps [r8 + i], xmm9
                    %assign i i + 16
                %endrep
                add r8, 2 << %1
                cmp r8, r9
                jl .internal_loop
            psubw xmm_c, xmm7

        .end_loop
            psubw xmm_c, xmm_vba
            xor r6d, r6d
        .single_line
            mov r10d, r7d
            sub r10d, r6d  ; up_pos - dn_pos
            add r6d, r7d  ; up_pos + dn_pos
            FILL_BORDER_LINE %1, r8, 10,6, 4,5, 7,8,9,10,11,12

        .end_line_loop
            add r2, SIZEOF_SEGMENT
            sub r3, 1
            jnz .line_loop

        mov r2, rsp
        mov r3d, 1 << %1
        lea r4, [rsp + DELTA_OFFS]
        xor r5d, r5d
        .fill_loop
            add r5w, [r4]
            movd xmm7, r5d
            pshufb xmm7, xmm_bcast  ; SSSE3
            add r4, 2

            %assign i 0
            %rep (1 << %1) / 16
                movaps xmm5, [r2 + 2 * i]
                paddw xmm5, xmm7
                pabsw xmm5, xmm5
                movaps xmm6, [r2 + 2 * i + 16]
                paddw xmm6, xmm7
                pabsw xmm6, xmm6
                packuswb xmm5, xmm6
                movaps [r0 + i], xmm5
                %assign i i + 16
            %endrep

            add r0, r1
            add r2, 2 << %1
            sub r3d,1
            jnz .fill_loop
        RET
%endmacro

INIT_XMM sse2
FILL_GENERIC_TILE 4,16
INIT_XMM sse2
FILL_GENERIC_TILE 5,32

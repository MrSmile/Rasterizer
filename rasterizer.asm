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
        %define op add
        %define opi sub
        %assign flag 1 << 2
    %elifidn %1, dn
        %define op sub
        %define opi add
        %assign flag 1 << 3
    %else
        %error "up/dn expected!"
    %endif

    test r%3d, flag
    jz %%skip
    lea r%5d, [4 * r%4d - 256]
    opi [%2], r%5w
    lea r%5d, [4 * r%4d]
    op [%2 + 2], r%5w
%%skip:

    %undef op
    %undef opi
    %undef flag
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
        movd xmm%12, r%5d
        pshufb xmm%12, xmm_bcast  ; SSSE3

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
    movd xmm%7, r%4d
    pshufb xmm%7, xmm_bcast  ; SSSE3
    imul r%6d, r%5d
    sar r%6d, 16  ; offs2 - offs1
    movd xmm%8, r%6d
    pshufb xmm%8, xmm_bcast  ; SSSE3
    add r%3d, r%3d
    movd xmm%9, r%3d
    pshufb xmm%9, xmm_bcast  ; SSSE3
    %if ARCH_X86_64 == 0
        imul r%5d, 0x10001
    %endif

    %assign i 0
    %rep (1 << %1) / 8
        %if i
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
        movaps xmm%11, [%2 + i]
        paddw xmm%10, xmm%11
        movaps [%2 + i], xmm%10
        %assign i i + 16
    %endrep
%endmacro

;------------------------------------------------------------------------------
; FILL_GENERIC_TILE tile_order, suffix
; void fill_generic_tile%2( uint8_t *buf, ptrdiff_t stride,
;                           const struct Segment *line, size_t n_lines,
;                           int winding )
;------------------------------------------------------------------------------

%if ARCH_X86_64

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
        mov r6, rstk
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
        mov [rstk + DELTA_OFFS], r4w

        movdqa xmm_bcast, [bcast_word]
        movdqa xmm_index, [words_index]
        movdqa xmm_full, [words_tile%2]

        .line_loop
            CALC_DELTA_FLAG 10, r2, 4,5

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

            UPDATE_DELTA dn, rstk + 2 * r8 + DELTA_OFFS, 10,6, 11
            UPDATE_DELTA up, rstk + 2 * r9 + DELTA_OFFS, 10,7, 11

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
            add r8, rstk
            shl r9d, 1 + %1
            add r9, rstk
            cmp r8, r9
            jz .single_line

            movd xmm_vba, r_b
            pshufb xmm_vba, xmm_bcast  ; SSSE3
            %rep (1 << %1) / 8 - 1
                psubw xmm_vba, xmm_va8  ; b - (tile_size - 8) * a
            %endrep

            test r6d, r6d
            jz .generic_fist
            mov r10d, 64
            sub r10d, r6d  ; 64 - dn_pos
            add r6d, 64  ; 64 + dn_pos
            FILL_BORDER_LINE %1, r8, 10,6, 4,5, 7,8,9,10,11,12
            psubw xmm_c, xmm_vba
            add r8, 2 << %1
            cmp r8, r9
            jge .end_loop

        .generic_fist
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
                psubw xmm_c, xmm_vba
                add r8, 2 << %1
                cmp r8, r9
                jl .internal_loop
            psubw xmm_c, xmm7

        .end_loop
            test r7d, r7d
            jz .end_line_loop
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

        mov r2, rstk
        mov r3d, 1 << %1
        lea r4, [rstk + DELTA_OFFS]
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

%else  ; ARCH_X86_64

%macro FILL_GENERIC_TILE 2
    %assign tile_size 1 << %1
    %define xmm_zero  xmm0
    %define xmm_va8   xmm1
    %define xmm_c     xmm2

    %define xmm_bcast xmm7
    %define xmm_index [words_index]
    %define xmm_full  xmm3
    %define xmm_vba   xmm4

    cglobal fill_generic_tile%2, 0,7,8, 2 * tile_size * (tile_size + 1) + 16
        pxor xmm_zero, xmm_zero
        mov r6, rstk
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

        mov r4w, r4m
        shl r4d, 8
        %assign DELTA_OFFS 2 * tile_size * tile_size
        mov [rstk + DELTA_OFFS], r4w

        %define up_addr [rstk + 2 * tile_size * (tile_size + 1) + 4]
        %define up_pos [rstk + 2 * tile_size * (tile_size + 1) + 8]

        .line_loop
            mov r3, r2m
            lea r2, [r3 + SIZEOF_SEGMENT]
            mov r2m, r2

            CALC_DELTA_FLAG 0, r3, 4,5

            mov r5d, [r3 + SEGOFFS_Y_MIN]
            mov r4d, [r3 + SEGOFFS_Y_MAX]
            lea r1d, [r0d + 1]
            cmp r5d, r4d
            cmovz r0d, r1d  ; bit 0 -- horz line

            mov r6d, r4d
            and r6d, 63  ; up_pos
            shr r4d, 6  ; up
            UPDATE_DELTA up, rstk + 2 * r4 + DELTA_OFFS, 0,6, 1
            shl r4d, 1 + %1
            add r4, rstk
            mov up_addr, r4
            mov up_pos, r6d

            mov r6d, r5d
            and r6d, 63  ; dn_pos
            shr r5d, 6  ; dn
            UPDATE_DELTA dn, rstk + 2 * r5 + DELTA_OFFS, 0,6, 1

            test r0d, 1
            jnz .end_line_loop

            mov r0d, [r3 + SEGOFFS_C]
            mov r2d, [r3 + SEGOFFS_C + 4]
            mov r1d, [r3 + SEGOFFS_SCALE]
            shr r0d, 7 + %1
            shl r2d, 25 - %1
            or r0d, r2d  ; r0d (eax) = c >> (tile_order + 7)
            imul r1d  ; r2d (edx) = (c >> ...) * scale >> 32
            add r2d, 1 << 12
            sar r2d, 13
            mov r4d, r2d  ; c
            mov r0d, [r3 + SEGOFFS_B]  ; r0d (eax)
            imul r1d  ; r2d (edx) = b * scale >> 32
            add r2d, 1 << (13 + %1)
            sar r2d, 14 + %1
            mov r0d, [r3 + SEGOFFS_A]  ; r0d (eax)
            mov r3d, r2d  ; b
            imul r1d  ; r2d (edx) = a * scale >> 32
            add r2d, 1 << (13 + %1)
            sar r2d, 14 + %1  ; a

            movdqa xmm_bcast, [bcast_word]

            mov r0d, r2d
            sar r0d, 1
            sub r4d, r0d
            mov r0d, r3d
            imul r0d, r5d
            sub r4d, r0d ; c
            movd xmm_c, r4d
            pshufb xmm_c, xmm_bcast  ; SSSE3

            movd xmm3, r2d  ; a
            pshufb xmm3, xmm_bcast  ; SSSE3
            movdqa xmm_va8, xmm3
            pmullw xmm3, xmm_index
            psubw xmm_c, xmm3  ; c - a * i
            psllw xmm_va8, 3  ; 8 * a

            mov r0d, r2d
            sar r0d, 31
            xor r2d, r0d
            sub r2d, r0d  ; abs_a
            mov r1d, r3d  ; b
            mov r0d, r1d
            sar r0d, 31
            xor r1d, r0d
            sub r1d, r0d  ; abs_b
            shl r1d, 16
            or r2d, r1d

            %define r_abs_a r2w
            %define r_abs_ab r2d
            %define r_b r3d
            shl r5d, 1 + %1
            add r5, rstk
            cmp r5, up_addr
            jz .single_line

            ;test r6d, r6d
            ;jz .generic_fist
            mov r4d, 64
            sub r4d, r6d  ; 64 - dn_pos
            add r6d, 64  ; 64 + dn_pos
            FILL_BORDER_LINE %1, r5, 4,6, 0,1, 3,4,5,6,7,--

            movdqa xmm_bcast, [bcast_word]
            mov r6, up_addr

            movd xmm_vba, r_b
            pshufb xmm_vba, xmm_bcast  ; SSSE3
            %rep (1 << %1) / 8 - 1
                psubw xmm_vba, xmm_va8  ; b - (tile_size - 8) * a
            %endrep

            psubw xmm_c, xmm_vba
            add r5, 2 << %1
            cmp r5, r6
            jge .end_loop

        .generic_fist
            mov r0d, 1 << (13 - %1)
            mov r4d, r_b
            sar r4d, 1
            sub r0d, r4d  ; base
            mov r1d, r_abs_ab
            shr r1d, 16  ; abs_b
            cmp r1w, r_abs_a
            cmovg r1w, r_abs_a
            add r1d, 2
            sar r1d, 2  ; dc
            sub r0w, r1w  ; base - dc
            add r1d, r1d  ; 2 * dc
            imul r0d, 0x10001
            movd xmm5, r1d
            pshufb xmm5, xmm_bcast  ; SSSE3

            movdqa xmm_full, [words_tile%2]

            movd xmm7, r0d
            pshufd xmm7, xmm7, 0
            paddw xmm_c, xmm7
            .internal_loop
                %assign i 0
                %rep (1 << %1) / 8
                    %if i
                        psubw xmm_c, xmm_va8
                    %endif
                    CALC_LINE 6, xmm_c,xmm5, xmm_zero,xmm_full, 7, %1
                    movaps xmm7, [r5 + i]
                    paddw xmm6, xmm7
                    movaps [r5 + i], xmm6
                    %assign i i + 16
                %endrep
                psubw xmm_c, xmm_vba
                add r5, 2 << %1
                cmp r5, r6
                jl .internal_loop
            movd xmm7, r0d
            pshufd xmm7, xmm7, 0
            psubw xmm_c, xmm7

        .end_loop
            ;test r7d, r7d
            ;jz .end_line_loop
            xor r6d, r6d
            movdqa xmm_bcast, [bcast_word]
        .single_line
            mov r0d, up_pos
            mov r4d, r0d
            sub r4d, r6d  ; up_pos - dn_pos
            add r6d, r0d  ; up_pos + dn_pos
            FILL_BORDER_LINE %1, r5, 4,6, 0,1, 3,4,5,6,7,--

        .end_line_loop
            sub dword r3m, 1
            jnz .line_loop


        mov r0, r0m
        mov r1, r1m
        movdqa xmm_bcast, [bcast_word]

        mov r2, rstk
        mov r3d, 1 << %1
        lea r4, [rstk + DELTA_OFFS]
        xor r5d, r5d
        .fill_loop
            add r5w, [r4]
            movd xmm0, r5d
            pshufb xmm0, xmm_bcast  ; SSSE3
            add r4, 2

            %assign i 0
            %rep (1 << %1) / 16
                movaps xmm1, [r2 + 2 * i]
                paddw xmm1, xmm0
                pabsw xmm1, xmm1
                movaps xmm2, [r2 + 2 * i + 16]
                paddw xmm2, xmm0
                pabsw xmm2, xmm2
                packuswb xmm1, xmm2
                movaps [r0 + i], xmm1
                %assign i i + 16
            %endrep

            add r0, r1
            add r2, 2 << %1
            sub r3d, 1
            jnz .fill_loop
        RET
%endmacro

%endif  ; ARCH_X86_64

INIT_XMM sse2
FILL_GENERIC_TILE 4,16
INIT_XMM sse2
FILL_GENERIC_TILE 5,32

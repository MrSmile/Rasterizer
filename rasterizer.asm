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
    .zerofill_loop
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
        jnz .zerofill_loop
    %assign i 0
    %rep (%2) & 7
        movaps [r%1 + i], xmm_zero
        %assign i i + 16
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
; SAVE_RESULT tile_order, buf, stride, src, delta,
;             tmp6, tmp7, xmm8, xmm9, xmm10, xmm11
;------------------------------------------------------------------------------

%macro SAVE_RESULT 11
    mov r%6d, 1 << %1
    xor r%7d, r%7d
    .save_loop
        add r%7w, [r%5]
        BROADCAST xmm%10, r%7d
        add r%5, 2

        %assign i 0
        %rep (1 << %1) / 16
            movaps xmm%8, [r%4 + 2 * i]
            paddw xmm%8, xmm%10
            pxor xmm%11, xmm%11
            psubw xmm%11, xmm%8
            pmaxsw xmm%8, xmm%11
            movaps xmm%9, [r%4 + 2 * i + 16]
            paddw xmm%9, xmm%10
            pxor xmm%11, xmm%11
            psubw xmm%11, xmm%9
            pmaxsw xmm%9, xmm%11
            packuswb xmm%8, xmm%9
            movaps [r%2 + i], xmm%8
            %assign i i + 16
        %endrep

        add r%2, r%3
        add r%4, 2 << %1
        sub r%6d, 1
        jnz .save_loop
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
    %define xmm_index xmm2
    %define xmm_va8   xmm3
    %define xmm_vba   xmm4
    %define xmm_c     xmm5

    cglobal fill_generic_tile%2, 5,15,12
        %assign alloc_size 2 * tile_size * (tile_size + 1) + 4
        %if HAVE_ALIGNED_STACK
            %assign alloc_size ((alloc_size + stack_offset + gprsize + 15) & ~15) - stack_offset - gprsize
        %else
            %assign alloc_size alloc_size + 32
        %endif
        SUB rstk, alloc_size

        %assign n tile_size * (tile_size + 1) / 8
        %if HAVE_ALIGNED_STACK
            mov r6, rstk
        %else
            %assign n n + 1
            lea r6, [rstk + 15]
            and r6, ~15
        %endif
        pxor xmm_zero, xmm_zero
        ZEROFILL 6, n, 5

        shl r4d, 8
        %assign delta_offs 2 * tile_size * tile_size
        %if HAVE_ALIGNED_STACK == 0
            %assign delta_offs delta_offs + 16
        %endif
        mov [rstk + delta_offs], r4w

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

            UPDATE_DELTA dn, rstk + 2 * r8 + delta_offs, 10,6, 11
            UPDATE_DELTA up, rstk + 2 * r9 + delta_offs, 10,7, 11

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
            BROADCAST xmm_c, r13d

            BROADCAST xmm6, r11d  ; a
            movdqa xmm_va8, xmm6
            pmullw xmm6, xmm_index
            psubw xmm_c, xmm6  ; c - a * i
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
            shl r9d, 1 + %1
            %if HAVE_ALIGNED_STACK
                add r8, rstk
                add r9, rstk
            %else
                lea r4, [rstk + 15]
                and r4, ~15
                add r8, r4
                add r9, r4
            %endif
            cmp r8, r9
            jz .single_line

            BROADCAST xmm_vba, r_b
            %rep (1 << %1) / 8 - 1
                psubw xmm_vba, xmm_va8  ; b - (tile_size - 8) * a
            %endrep

            test r6d, r6d
            jz .generic_fist
            mov r10d, 64
            sub r10d, r6d  ; 64 - dn_pos
            add r6d, 64  ; 64 + dn_pos
            FILL_BORDER_LINE %1, r8,10,6, 4,5, 6,7,8,9,10,11
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
            BROADCAST xmm6, r4d
            BROADCAST xmm7, r5d

            paddw xmm_c, xmm6
            .internal_loop
                %assign i 0
                %rep (1 << %1) / 8
                    %if i
                        psubw xmm_c, xmm_va8
                    %endif
                    CALC_LINE %1, 8, xmm_c,xmm7, xmm_zero,xmm_full, 9
                    movaps xmm9, [r8 + i]
                    paddw xmm8, xmm9
                    movaps [r8 + i], xmm8
                    %assign i i + 16
                %endrep
                psubw xmm_c, xmm_vba
                add r8, 2 << %1
                cmp r8, r9
                jl .internal_loop
            psubw xmm_c, xmm6

        .end_loop
            test r7d, r7d
            jz .end_line_loop
            xor r6d, r6d
        .single_line
            mov r10d, r7d
            sub r10d, r6d  ; up_pos - dn_pos
            add r6d, r7d  ; up_pos + dn_pos
            FILL_BORDER_LINE %1, r8,10,6, 4,5, 6,7,8,9,10,11

        .end_line_loop
            add r2, SIZEOF_SEGMENT
            sub r3, 1
            jnz .line_loop

        %if HAVE_ALIGNED_STACK
            mov r2, rstk
        %else
            lea r2, [rstk + 15]
            and r2, ~15
        %endif
        lea r3, [rstk + delta_offs]
        SAVE_RESULT %1, 0,1,2,3, 4,5, 0,1,2,3
        ADD rstk, alloc_size
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

    cglobal fill_generic_tile%2, 0,7,8
        %assign alloc_size 2 * tile_size * (tile_size + 1) + 4 + 8
        %if HAVE_ALIGNED_STACK
            %assign alloc_size ((alloc_size + stack_offset + gprsize + 15) & ~15) - stack_offset - gprsize
        %else
            %assign alloc_size alloc_size + 32
        %endif
        SUB rstk, alloc_size

        %assign n tile_size * (tile_size + 1) / 8
        %if HAVE_ALIGNED_STACK
            mov r6, rstk
        %else
            %assign n n + 1
            lea r6, [rstk + 15]
            and r6, ~15
        %endif
        pxor xmm_zero, xmm_zero
        ZEROFILL 6, n, 5

        mov r4w, r4m
        shl r4d, 8
        %assign delta_offs 2 * tile_size * tile_size
        %if HAVE_ALIGNED_STACK == 0
            %assign delta_offs delta_offs + 16
        %endif
        mov [rstk + delta_offs], r4w

        %define up_addr [rstk + delta_offs + 2 * tile_size + 4]
        %define up_pos [rstk + delta_offs + 2 * tile_size + 8]

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
            UPDATE_DELTA up, rstk + 2 * r4 + delta_offs, 0,6, 1
            shl r4d, 1 + %1
            %if HAVE_ALIGNED_STACK
                add r4, rstk
            %else
                lea r1, [rstk + 15]
                and r1, ~15
                add r4, r1
            %endif
            mov up_addr, r4
            mov up_pos, r6d

            mov r6d, r5d
            and r6d, 63  ; dn_pos
            shr r5d, 6  ; dn
            UPDATE_DELTA dn, rstk + 2 * r5 + delta_offs, 0,6, 1

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

            mov r0d, r2d
            sar r0d, 1
            sub r4d, r0d
            mov r0d, r3d
            imul r0d, r5d
            sub r4d, r0d ; c
            BROADCAST xmm_c, r4d

            BROADCAST xmm3, r2d  ; a
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
            %if HAVE_ALIGNED_STACK
                add r5, rstk
            %else
                lea r0, [rstk + 15]
                and r0, ~15
                add r5, r0
            %endif
            cmp r5, up_addr
            jz .single_line

            test r6d, r6d
            jz .generic_fist
            mov r4d, 64
            sub r4d, r6d  ; 64 - dn_pos
            add r6d, 64  ; 64 + dn_pos
            FILL_BORDER_LINE %1, r5, 4,6, 0,1, 3,4,5,6,7,--

            mov r6, up_addr
            BROADCAST xmm_vba, r_b
            %rep (1 << %1) / 8 - 1
                psubw xmm_vba, xmm_va8  ; b - (tile_size - 8) * a
            %endrep

            psubw xmm_c, xmm_vba
            add r5, 2 << %1
            cmp r5, r6
            jge .end_loop
            jmp .bulk_fill

        .generic_fist
            mov r6, up_addr
            BROADCAST xmm_vba, r_b
            %rep (1 << %1) / 8 - 1
                psubw xmm_vba, xmm_va8  ; b - (tile_size - 8) * a
            %endrep

        .bulk_fill
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
            BROADCAST xmm5, r1d

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
                    CALC_LINE %1, 6, xmm_c,xmm5, xmm_zero,xmm_full, 7
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
            mov r4d, up_pos
            test r4d, r4d
            jz .end_line_loop
            mov r6d, r4d
            jmp .last_line

        .single_line
            mov r0d, up_pos
            mov r4d, r0d
            sub r4d, r6d  ; up_pos - dn_pos
            add r6d, r0d  ; up_pos + dn_pos
        .last_line
            FILL_BORDER_LINE %1, r5, 4,6, 0,1, 3,4,5,6,7,--

        .end_line_loop
            sub dword r3m, 1
            jnz .line_loop


        mov r0, r0m
        mov r1, r1m
        %if HAVE_ALIGNED_STACK
            mov r2, rstk
        %else
            lea r2, [rstk + 15]
            and r2, ~15
        %endif
        lea r3, [rstk + delta_offs]
        SAVE_RESULT %1, 0,1,2,3, 4,5, 0,1,2,3
        ADD rstk, alloc_size
        RET
%endmacro

%endif  ; ARCH_X86_64

INIT_XMM sse2
FILL_GENERIC_TILE 4,16
INIT_XMM sse2
FILL_GENERIC_TILE 5,32

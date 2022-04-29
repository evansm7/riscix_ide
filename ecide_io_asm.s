@ Low-level sector transfer ops for IDE podules
@ FIXME: Add ops for 8b podules
@
@ Copyright (c) 2022 Matt Evans
@
@ Permission is hereby granted, free of charge, to any person obtaining a copy
@ of this software and associated documentation files (the "Software"), to deal
@ in the Software without restriction, including without limitation the rights
@ to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
@ copies of the Software, and to permit persons to whom the Software is
@ furnished to do so, subject to the following conditions:
@
@ The above copyright notice and this permission notice shall be included in all
@ copies or substantial portions of the Software.
@
@ THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
@ IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
@ FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
@ AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
@ LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
@ OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
@ SOFTWARE.


fp      .req    r11
ip      .req    r12
sp      .req    r13
lr      .req    r14
pc      .req    r15

        .text

        .global _ide_read_data
        @ r0 = IO regs
        @ r1 = destination buffer
        @ Read 512 bytes from the 16b data reg.
_ide_read_data:
        stmfd   sp!,{r4-r7}
        mov     r2, #0
1:
        ldr     r3, [r0, #0]
        mov     r3, r3, lsl #16
        mov     r3, r3, lsr #16
        ldr     r4, [r0, #0]
        orr     r4, r3, r4, lsl #16

        ldr     r3, [r0, #0]
        mov     r3, r3, lsl #16
        mov     r3, r3, lsr #16
        ldr     r5, [r0, #0]
        orr     r5, r3, r5, lsl #16

        ldr     r3, [r0, #0]
        mov     r3, r3, lsl #16
        mov     r3, r3, lsr #16
        ldr     r6, [r0, #0]
        orr     r6, r3, r6, lsl #16

        ldr     r3, [r0, #0]
        mov     r3, r3, lsl #16
        mov     r3, r3, lsr #16
        ldr     r7, [r0, #0]
        orr     r7, r3, r7, lsl #16

        stmia   r1!, {r4, r5, r6, r7}

        add     r2, r2, #1
        cmp     r2, #(512/4/4)
        blt     1b
        ldmfd   sp!,{r4-r7}
        movs    pc, lr


        .global _ide_read_data8
        @ r0 = IO regs
        @ r1 = High-byte latch
        @ r2 = destination buffer
        @ Read 512 bytes from the 8b data reg + 8b HBL.
_ide_read_data8:
        stmfd   sp!,{r4-r8}
        mov     r8, #0
1:
        ldrb    r3, [r0, #0]
        ldrb    r4, [r1]
        orr     r3, r3, r4, lsl #8
        ldrb    r4, [r0, #0]
        orr     r3, r3, r4, lsl #16
        ldrb    r4, [r1]
        orr     r4, r3, r4, lsl #24

        ldrb    r3, [r0, #0]
        ldrb    r5, [r1]
        orr     r3, r3, r5, lsl #8
        ldrb    r5, [r0, #0]
        orr     r3, r3, r5, lsl #16
        ldrb    r5, [r1]
        orr     r5, r3, r5, lsl #24

        ldrb    r3, [r0, #0]
        ldrb    r6, [r1]
        orr     r3, r3, r6, lsl #8
        ldrb    r6, [r0, #0]
        orr     r3, r3, r6, lsl #16
        ldrb    r6, [r1]
        orr     r6, r3, r6, lsl #24

        ldrb    r3, [r0, #0]
        ldrb    r7, [r1]
        orr     r3, r3, r7, lsl #8
        ldrb    r7, [r0, #0]
        orr     r3, r3, r7, lsl #16
        ldrb    r7, [r1]
        orr     r7, r3, r7, lsl #24

        stmia   r2!, {r4, r5, r6, r7}

        add     r8, r8, #1
        cmp     r8, #(512/4/4)
        blt     1b
        ldmfd   sp!,{r4-r8}
        movs    pc, lr


        .global _ide_write_data
        @ r0 = IO regs
        @ r1 = source buffer
_ide_write_data:
        stmfd   sp!,{r4-r7}
        mov     r2, #0
1:
        ldmia   r1!, {r4-r7}

        mov     r3, r4, lsl#16  @ Store low hword to D[31:16]
        str     r3, [r0, #0]
        str     r4, [r0, #0]    @ Store high hword to D[31:16]

        mov     r3, r5, lsl#16
        str     r3, [r0, #0]
        str     r5, [r0, #0]

        mov     r3, r6, lsl#16
        str     r3, [r0, #0]
        str     r6, [r0, #0]

        mov     r3, r7, lsl#16
        str     r3, [r0, #0]
        str     r7, [r0, #0]

        add     r2, r2, #1
        cmp     r2, #(512/4/4)
        blt     1b
        ldmfd   sp!,{r4-r7}
        movs    pc, lr


        .global _ide_write_data8
        @ r0 = IO regs
        @ r1 = High-byte latch
        @ r2 = source buffer
_ide_write_data8:
        stmfd   sp!,{r4-r8}
        mov     r8, #0
1:
        ldmia   r2!, {r4-r7}

        mov     r3, r4, lsr#8
        strb    r3, [r1]
        strb    r4, [r0, #0]
        mov     r3, r4, lsr#24
        mov     r4, r4, lsr#16
        strb    r3, [r1]
        strb    r4, [r0, #0]

        mov     r3, r5, lsr#8
        strb    r3, [r1]
        strb    r5, [r0, #0]
        mov     r3, r5, lsr#24
        mov     r5, r5, lsr#16
        strb    r3, [r1]
        strb    r5, [r0, #0]

        mov     r3, r6, lsr#8
        strb    r3, [r1]
        strb    r6, [r0, #0]
        mov     r3, r6, lsr#24
        mov     r6, r6, lsr#16
        strb    r3, [r1]
        strb    r6, [r0, #0]

        mov     r3, r7, lsr#8
        strb    r3, [r1]
        strb    r7, [r0, #0]
        mov     r3, r7, lsr#24
        mov     r7, r7, lsr#16
        strb    r3, [r1]
        strb    r7, [r0, #0]

        add     r8, r8, #1
        cmp     r8, #(512/4/4)
        blt     1b
        ldmfd   sp!,{r4-r8}
        movs    pc, lr

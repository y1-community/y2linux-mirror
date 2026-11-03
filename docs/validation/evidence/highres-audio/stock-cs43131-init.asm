
/home/luca/Dokumente/Code/Y2Player/reverse/ghidra/exports/y2-kernel-with-kallsyms.elf:	file format elf32-littlearm

Disassembly of section .kernel:

c054b408 <cs43131_hw_init>:
c054b408: e1a0c00d     	mov	r12, sp
c054b40c: e92dd818     	push	{r3, r4, r11, r12, lr, pc}
c054b410: e24cb004     	sub	r11, r12, #4
c054b414: e59f311c     	ldr	r3, [pc, #0x11c]        @ 0xc054b538 <cs43131_hw_init+0x130>
c054b418: e5d33000     	ldrb	r3, [r3]
c054b41c: e3530000     	cmp	r3, #0
c054b420: 1a000003     	bne	0xc054b434 <cs43131_hw_init+0x2c> @ imm = #0xc
c054b424: e59f1110     	ldr	r1, [pc, #0x110]        @ 0xc054b53c <cs43131_hw_init+0x134>
c054b428: e59f0110     	ldr	r0, [pc, #0x110]        @ 0xc054b540 <cs43131_hw_init+0x138>
c054b42c: eb0660f8     	bl	0xc06e3814 <printk>     @ imm = #0x1983e0
c054b430: e89da818     	ldm	sp, {r3, r4, r11, sp, pc}
c054b434: e59f0108     	ldr	r0, [pc, #0x108]        @ 0xc054b544 <cs43131_hw_init+0x13c>
c054b438: eb0660f5     	bl	0xc06e3814 <printk>     @ imm = #0x1983d4
c054b43c: e3a01000     	mov	r1, #0
c054b440: e3a00802     	mov	r0, #131072
c054b444: e59f40fc     	ldr	r4, [pc, #0xfc]         @ 0xc054b548 <cs43131_hw_init+0x140>
c054b448: ebfffe5d     	bl	0xc054adc4 <cs43131_write_byte> @ imm = #-0x68c
c054b44c: e3a00014     	mov	r0, #20
c054b450: ebec2ab9     	bl	0xc0055f3c <msleep>     @ imm = #-0x4f551c
c054b454: e3a01004     	mov	r1, #4
c054b458: e3a00006     	mov	r0, #6
c054b45c: e3400001     	movt	r0, #0x1
c054b460: ebfffe57     	bl	0xc054adc4 <cs43131_write_byte> @ imm = #-0x6a4
c054b464: e3a01001     	mov	r1, #1
c054b468: e3a0000b     	mov	r0, #11
c054b46c: e3400001     	movt	r0, #0x1
c054b470: ebfffe53     	bl	0xc054adc4 <cs43131_write_byte> @ imm = #-0x6b4
c054b474: e3a01005     	mov	r1, #5
c054b478: e3a0000c     	mov	r0, #12
c054b47c: e3400001     	movt	r0, #0x1
c054b480: ebfffe4f     	bl	0xc054adc4 <cs43131_write_byte> @ imm = #-0x6c4
c054b484: e3a0100c     	mov	r1, #12
c054b488: e3a00018     	mov	r0, #24
c054b48c: e3400004     	movt	r0, #0x4
c054b490: ebfffe4b     	bl	0xc054adc4 <cs43131_write_byte> @ imm = #-0x6d4
c054b494: e3a01000     	mov	r1, #0
c054b498: e3a00805     	mov	r0, #327680
c054b49c: ebfffe48     	bl	0xc054adc4 <cs43131_write_byte> @ imm = #-0x6e0
c054b4a0: e3a01000     	mov	r1, #0
c054b4a4: e3a00001     	mov	r0, #1
c054b4a8: e3400005     	movt	r0, #0x5
c054b4ac: ebfffe44     	bl	0xc054adc4 <cs43131_write_byte> @ imm = #-0x6f0
c054b4b0: e3a01006     	mov	r1, #6
c054b4b4: e3a0000a     	mov	r0, #10
c054b4b8: e3400005     	movt	r0, #0x5
c054b4bc: ebfffe40     	bl	0xc054adc4 <cs43131_write_byte> @ imm = #-0x700
c054b4c0: e3a0100e     	mov	r1, #14
c054b4c4: e3a0000b     	mov	r0, #11
c054b4c8: e3400005     	movt	r0, #0x5
c054b4cc: ebfffe3c     	bl	0xc054adc4 <cs43131_write_byte> @ imm = #-0x710
c054b4d0: e3a01002     	mov	r1, #2
c054b4d4: e3a00809     	mov	r0, #589824
c054b4d8: ebfffe39     	bl	0xc054adc4 <cs43131_write_byte> @ imm = #-0x71c
c054b4dc: e5d410d8     	ldrb	r1, [r4, #0xd8]
c054b4e0: e3a00001     	mov	r0, #1
c054b4e4: e3400009     	movt	r0, #0x9
c054b4e8: ebfffe35     	bl	0xc054adc4 <cs43131_write_byte> @ imm = #-0x72c
c054b4ec: e5d410d8     	ldrb	r1, [r4, #0xd8]
c054b4f0: e3a00002     	mov	r0, #2
c054b4f4: e3400009     	movt	r0, #0x9
c054b4f8: ebfffe31     	bl	0xc054adc4 <cs43131_write_byte> @ imm = #-0x73c
c054b4fc: e3a01000     	mov	r1, #0
c054b500: e3a00802     	mov	r0, #131072
c054b504: ebfffe2e     	bl	0xc054adc4 <cs43131_write_byte> @ imm = #-0x748
c054b508: e3a010ca     	mov	r1, #202
c054b50c: e3a0080d     	mov	r0, #851968
c054b510: ebfffe2b     	bl	0xc054adc4 <cs43131_write_byte> @ imm = #-0x754
c054b514: e3a0109f     	mov	r1, #159
c054b518: e3a00010     	mov	r0, #16
c054b51c: e340000f     	movt	r0, #0xf
c054b520: ebfffe27     	bl	0xc054adc4 <cs43131_write_byte> @ imm = #-0x764
c054b524: e3a0000c     	mov	r0, #12
c054b528: ebec2a83     	bl	0xc0055f3c <msleep>     @ imm = #-0x4f55f4
c054b52c: e59f0018     	ldr	r0, [pc, #0x18]         @ 0xc054b54c <cs43131_hw_init+0x144>
c054b530: eb0660b7     	bl	0xc06e3814 <printk>     @ imm = #0x1982dc
c054b534: e89da818     	ldm	sp, {r3, r4, r11, sp, pc}
c054b538: c0dad458     	sbcsgt	sp, r10, r8, asr r4
c054b53c: c076083c     	rsbsgt	r0, r6, r12, lsr r8
c054b540: c09093a0     	addsgt	r9, r0, r0, lsr #7
c054b544: c09093b0     	ldrhgt	r9, [r0], r0
c054b548: c0a0c2e4     	adcgt	r12, r0, r4, ror #5
c054b54c: c09093e8     	addsgt	r9, r0, r8, ror #7

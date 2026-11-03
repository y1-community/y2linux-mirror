Stock libaudio.primary.default.so SHA256:
5c5162f6a68f7db57febd050ee88cc886779dcce5948937149d6cd211eb0e6de
Retained Y2 stock export; ELF virtual addresses, not firmware patch offsets.
Both exported functions resolve to the same eight-byte stub:
0x00031138 size 8 AudioDigitalControl::SetMemIfFetchFormatPerSample(unsigned int, AudioMEMIFAttribute::FETCHFORMATPERSAMPLE)
0x00031138 size 8 AudioDigitalControl::SetoutputConnectionFormat(AudioDigitalType::OUTPUT_DATA_FORMAT, AudioDigitalType::InterConnectionOutput)
ARM mvn r0,#37 returns signed -38 (ENOSYS); bx lr returns without register I/O.


/home/luca/Dokumente/Code/Y2Player/out/hardware-snapshots/2026-07-29_004935/pulled/system__lib__libaudio.primary.default.so:	file format elf32-littlearm

Disassembly of section .text:

00031138 <_ZN7android19AudioDigitalControl28SetMemIfFetchFormatPerSampleEjN19AudioMEMIFAttribute20FETCHFORMATPERSAMPLEE>:
   31138: e3e00025     	mvn	r0, #37
   3113c: e12fff1e     	bx	lr

00031140 <_ZNK7android16AudioMTKStreamIn18getInputFramesLostEv>:
   31140: e3a00000     	mov	r0, #0
   31144: e12fff1e     	bx	lr

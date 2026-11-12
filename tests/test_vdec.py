"""Video decoder: power-domain sequence, device tree node and interface layout; no hardware access."""
from pathlib import Path
import struct
import sys
import unittest
from test_power import run_c

ROOT = Path(__file__).resolve().parents[1]
PLATFORM = ROOT / 'kernel/platform'
sys.path.insert(0, str(ROOT / 'tools/development/vdec'))


class VideoDecoder(unittest.TestCase):
    def test_domain_sequence_touches_only_its_register(self):
        run_c(r"""
#include <assert.h>
#include <errno.h>
#include "gpu-policy.h"
struct hardware { unsigned r[1024]; int stuck; };
static unsigned read_reg(void *ctx,unsigned reg){return ((struct hardware *)ctx)->r[reg/4];}
static void write_reg(void *ctx,unsigned reg,unsigned value){
 struct hardware *h=ctx;
 assert(reg==0 || reg==0x210); /* VDE_PWR_CON only: never MFG, display, CPU or radio. */
 if(reg==0){assert(value==0x0b160001);return;}
 assert(((h->r[reg/4]^value)&~0xf1fU)==0);
 h->r[reg/4]=value;
 if((value&0xf00)==0xf00)h->r[reg/4]|=0x1000; else h->r[reg/4]&=~0x1000U;
 if(!h->stuck){
  h->r[0x60c/4]=(h->r[0x60c/4]&~0x80U)|((value&12)==12?0x80:0);
  h->r[0x610/4]=(h->r[0x610/4]&~0x80U)|((value&12)==12?0x80:0);
 }
}
static void delay(unsigned us){(void)us;}
int main(void){
 struct hardware h={0};struct y2_spm_io io={&h,read_reg,write_reg,delay};
 h.r[0x210/4]=0x1f12;h.r[0x60c/4]=h.r[0x610/4]=0x3f1c;
 for(unsigned cycle=0;cycle<20;cycle++){
  assert(!y2_mtcmos_sequence(&io,0x210,0x80,1));assert(h.r[0x210/4]==13);
  assert(h.r[0x60c/4]==0x3f9c && h.r[0x610/4]==0x3f9c);
  assert(!y2_mtcmos_sequence(&io,0x210,0x80,0));assert(h.r[0x210/4]==0x1f12);
  assert(h.r[0x60c/4]==0x3f1c && h.r[0x610/4]==0x3f1c);
 }
 /* No acknowledgement: isolation and reset stay as they were. */
 h=(struct hardware){0};h.r[0x210/4]=0x1f12;h.stuck=1;
 assert(y2_mtcmos_sequence(&io,0x210,0x80,1)==-ETIMEDOUT);
 assert(h.r[0x210/4]==0x1f1e);
}
""")

    def test_device_tree_node_is_the_stock_decoder(self):
        dts = (ROOT / 'kernel/dts/innioasis-y2.dts').read_text()
        node = dts[dts.index('vdec: video-decoder@16000000'):]
        node = node[:node.index('};')]
        self.assertIn('reg = <0x16000000 0x1000>, <0x16020000 0x9000>;', node)
        # Stock MT_VDEC_IRQ_ID is GIC hwirq 171, level low.
        self.assertIn('interrupts = <GIC_SPI 139 IRQ_TYPE_LEVEL_LOW>;', node)
        self.assertIn('clocks = <&mmsys CLK_MM_SMI_COMMON>;', node)

    def test_power_goes_off_when_idle_closed_and_asleep(self):
        source = (PLATFORM / 'vdec.c').read_text()
        for owner in ('static void y2_vdec_idle', 'static int y2_vdec_release', 'static int y2_vdec_suspend',
                      'static void y2_vdec_remove'):
            body = source[source.index(owner):]
            self.assertIn('y2_vdec_power_off(v);', body[:body.index('\n}\n')], owner)
        off = source[source.index('static void y2_vdec_power_off'):source.index('static int y2_vdec_power_on')]
        self.assertLess(off.index('disable_irq'), off.index('y2_spm_vde_power(0)'))
        on = source[source.index('static int y2_vdec_power_on'):source.index('static void y2_vdec_idle')]
        self.assertLess(on.index('y2_spm_vde_power(1)'), on.index('enable_irq'))

    def test_job_tool_packs_the_interface_structure(self):
        import h264_jobs
        header = (PLATFORM / 'y2-vdec.h').read_text()
        self.assertEqual(h264_jobs.PICTURE.size, 464)
        self.assertEqual(h264_jobs.CONFIG.size, 32)
        self.assertEqual(struct.calcsize('<2BH2i'), 12)
        self.assertIn('struct y2_vdec_h264_ref ref[16];', header)
        self.assertIn('__u8 scaling4[6][16], scaling8[2][64];', header)

    def test_mpeg4_structure_and_the_copy_in_the_ffmpeg_patch(self):
        import mpeg4_jobs
        header = (PLATFORM / 'y2-vdec.h').read_text()
        self.assertEqual(mpeg4_jobs.PICTURE.size, 184)
        self.assertIn('__u8 intra_matrix[64], inter_matrix[64];', header)
        patch = (ROOT / 'buildroot/patches/ffmpeg/0001-y2-vdec-h264-hwaccel.patch').read_text()
        copy = patch[patch.index('+++ b/libavcodec/y2_vdec_uapi.h'):].split('\n')[2:]
        self.assertEqual('\n'.join(line[1:] for line in copy if line.startswith('+')) + '\n', header)
        # The picture comes from motion compensation, not from the deblocking post-processing stage.
        self.assertIn('vdec_write(v, VDEC_MC(0x220), 0);', (PLATFORM / 'vdec-mpeg4.c').read_text())

    def test_video_packet_headers_are_cut_from_b_pictures(self):
        import mpeg4_jobs

        def bits(text):
            text = text.replace(' ', '')
            text += '0' * (-len(text) % 8)
            return bytes(int(text[n:n + 8], 2) for n in range(0, len(text), 8))

        def stuffed(text):
            text = text.replace(' ', '')
            return bits(text + '0' + '1' * (-(len(text) + 1) % 8))

        fields = dict(fcode_f=1, fcode_b=1, width=32, height=32, quant=4, header_bits=40)
        # 17 zeros and a one, macroblock number (2 bits), quantiser, no extension.
        packet = '0' * 17 + '1' + '10' + '00100' + '0'
        start = '0' * 23 + '1' + '10110110' + '10101010'
        first, second = '110010', '1011' * 9
        # The header and the stuffing before it go; the stuffing at the end is written again.
        unit = stuffed(start + first + '01' + packet + second)
        self.assertEqual(mpeg4_jobs.strip_packets(fields, unit), stuffed(start + first + second))
        # A header in the middle of a row, or with another quantiser, keeps the picture as it is.
        for other in ('0' * 17 + '1' + '01' + '00100' + '0', '0' * 17 + '1' + '10' + '00101' + '0'):
            unit = stuffed(start + first + '01' + other + second)
            self.assertEqual(mpeg4_jobs.strip_packets(fields, unit), unit)

if __name__ == '__main__':
    unittest.main()

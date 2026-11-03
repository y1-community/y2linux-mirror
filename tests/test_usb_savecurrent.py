"""POWER-FINAL-13 USB PHY save-current across system sleep. Synthetic evidence only.

The expected write transcript is the Y2 FM kernel's usb_phy_savecurrent_internal
(0xc04d98f0), decoded from the retained stock image."""
from pathlib import Path
import unittest
from test_power import run_c

ROOT = Path(__file__).resolve().parents[1]
FAKE = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../usb/savecurrent.h"
static unsigned char phy[0x100];
static char log_[128][24];static unsigned logs;
static void note(const char *s){assert(logs<128);snprintf(log_[logs++],24,"%s",s);}
static unsigned rd(void *c,unsigned o){(void)c;return phy[o];}
static void wr(void *c,unsigned o,unsigned v){char s[24];(void)c;assert(v<256);phy[o]=v;snprintf(s,24,"%02x=%02x",o,v);note(s);}
static void dl(void *c,unsigned us){char s[24];(void)c;snprintf(s,24,"udelay%u",us);note(s);}
static const struct y2_phy_io io={0,rd,wr,dl};
'''


class SaveCurrent(unittest.TestCase):
    def test_sequence_matches_stock_usb_phy_savecurrent_internal(self):
        # Start from the recovered state Y2 proves at boot (recover.h clears these).
        run_c(FAKE + r'''
int main(void) {
 unsigned char saved[Y2_PHY_SAVE_REGS];
 phy[0x1a]=0x10; phy[0x63]=0x00; phy[0x68]=0x00; phy[0x69]=0x00; phy[0x6a]=0x00;
 phy[0x6b]=0x00; phy[0x6c]=0x00; phy[0x6d]=0x00; phy[0x6e]=0x00;
 y2_phy_save(&io,saved);
 y2_phy_savecurrent(&io);
 static const char *stock[]={"6b=00","6e=00","6a=00","68=c0","68=c0","68=d0","68=d4","69=00",
  "6a=ba","1a=10","1a=00","udelay800","63=02","udelay1","6a=be","udelay1","6c=00","6c=2e","6d=3e"};
 assert(logs==sizeof(stock)/sizeof(stock[0]));
 for(unsigned i=0;i<logs;i++){ if(strcmp(log_[i],stock[i])){printf("%u %s != %s\n",i,log_[i],stock[i]);return 1;} }
 /* Final state: PHY suspended (force_suspendm), BC1.1 and VBUS comparator off. */
 assert(phy[0x6a]==0xbe && phy[0x68]==0xd4 && phy[0x1a]==0x00 && phy[0x63]==0x02);
 return 0;
}
''')

    def test_restore_is_exact_and_releases_suspendm_before_other_forces(self):
        run_c(FAKE + r'''
int main(void) {
 static const unsigned char before[9]={0x10,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00};
 unsigned char saved[Y2_PHY_SAVE_REGS];
 for(unsigned i=0;i<9;i++) phy[y2_phy_save_offsets[i]]=before[i];
 y2_phy_save(&io,saved);
 y2_phy_savecurrent(&io);
 logs=0;
 assert(y2_phy_restore(&io,saved)==0);
 for(unsigned i=0;i<9;i++) assert(phy[y2_phy_save_offsets[i]]==before[i]);
 /* udelay50, 0x6b, 0x6e, then 0x6a with only force_suspendm released (0xbe->0xba). */
 assert(!strcmp(log_[0],"udelay50") && !strcmp(log_[1],"6b=00") && !strcmp(log_[2],"6e=00"));
 assert(!strcmp(log_[3],"6a=ba") && !strcmp(log_[4],"68=00") && !strcmp(log_[5],"69=00"));
 assert(!strcmp(log_[6],"6a=00") && !strcmp(log_[7],"1a=10") && !strcmp(log_[8],"63=00"));
 assert(!strcmp(log_[9],"udelay800") && !strcmp(log_[10],"6c=00") && !strcmp(log_[11],"6d=00"));
 /* A session-time forced 0x6c/0x6d value is restored as it was, not as recover.h's zeros. */
 phy[0x6c]=0x2e; phy[0x6d]=0x3e; y2_phy_save(&io,saved); y2_phy_savecurrent(&io);
 assert(y2_phy_restore(&io,saved)==0 && phy[0x6c]==0x2e && phy[0x6d]==0x3e);
 return 0;
}
''')

    def test_placement_only_in_parent_noirq_without_cable_or_session(self):
        source = (ROOT / 'kernel/platform/usb.c').read_text()
        suspend = source[source.index('static int y2_usb_suspend_noirq('):source.index('static int y2_usb_resume_noirq(')]
        for guard in ('y2_phy_savecurrent_enabled', 'y2_usb_finished', 'y2_live.result',
                      'y2_usb_child && !READ_ONCE(y2_usb_detached)', 'power.chrdet & 0x20'):
            self.assertIn(guard, suspend)
        self.assertLess(suspend.index('y2_phy_save(&y2_phy_io'), suspend.index('y2_phy_savecurrent(&y2_phy_io)'))
        self.assertIn('.suspend_noirq = y2_usb_suspend_noirq, .resume_noirq = y2_usb_resume_noirq', source)
        self.assertIn('&dev_attr_phy_savecurrent.attr', source)
        self.assertEqual(source.count('y2_phy_savecurrent(&y2_phy_io)'), 1)


if __name__ == '__main__':
    unittest.main()

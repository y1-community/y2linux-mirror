"""Real rescue UI input and framebuffer code; no hardware or kernel writes."""
from pathlib import Path
import unittest
from test_power import run_c
ROOT=Path(__file__).resolve().parents[1]

class OfflineCharging(unittest.TestCase):
    def test_button_requires_deliberate_fresh_press_and_safe_sample(self):
        run_c('#define main unused_rescue_main\n#include "'+str(ROOT/'tools/production/offline-charge.c')+'"\n#undef main\n'+r'''
#include <assert.h>
static struct input_event event(unsigned type,unsigned code,int value){return (struct input_event){.type=type,.code=code,.value=value};}
int main(void){
 struct power_interaction p={0};
 struct input_event down=event(EV_KEY,KEY_POWER,1),up=event(EV_KEY,KEY_POWER,0);
 struct input_event repeat=event(EV_KEY,KEY_POWER,2),volume=event(EV_KEY,KEY_VOLUMEUP,1);
 struct input_event dropped=event(EV_SYN,SYN_DROPPED,0),report=event(EV_SYN,SYN_REPORT,0);
 assert(!power_event(&p,&up,9000)); /* button already held at boot cannot boot */
 assert(!power_event(&p,&volume,10000) && !p.pressed);
 assert(!power_event(&p,&down,10000) && p.pressed);
 assert(!power_event(&p,&repeat,12500)); /* held alone must never boot */
 assert(power_event(&p,&up,12500) && !p.pressed);
 assert(!power_event(&p,&up,12501));
 assert(!power_event(&p,&down,20000));
 assert(!power_event(&p,&up,21999) && p.show_until==29999);
 assert(!power_event(&p,&down,30000));
 assert(!power_event(&p,&dropped,32000));
 assert(!power_event(&p,&up,33000));
 assert(!power_event(&p,&down,34000));
 assert(!power_event(&p,&report,34000));
 assert(!power_event(&p,&up,37000));
 assert(!power_event(&p,&down,40000));assert(power_event(&p,&up,42000));
 assert(!safe_boot_sample(0,1,3600000));assert(!safe_boot_sample(1,0,3600000));
 assert(!safe_boot_sample(-1,1,3600000));assert(!safe_boot_sample(1,1,3399999));
 assert(safe_boot_sample(1,1,3400000));
}
''')

    def test_framebuffer_bounds_and_no_percentage(self):
        run_c('#define main unused_rescue_main\n#include "'+str(ROOT/'tools/production/offline-charge.c')+'"\n#undef main\n'+r'''
#include <assert.h>
int main(void){
 unsigned char memory[480*360*4+16];memset(memory,0xa5,sizeof memory);
 pixels=memory+8;fix.line_length=480*4;fix.smem_len=480*360*4;
 var.xres=480;var.yres=360;var.bits_per_pixel=32;
 var.red=(struct fb_bitfield){16,8,0};var.green=(struct fb_bitfield){8,8,0};var.blue=(struct fb_bitfield){0,8,0};
 for(unsigned frame=0;frame<6;frame++)draw("Charging\n",false,false,frame*500);
 for(unsigned i=0;i<8;i++)assert(memory[i]==0xa5 && memory[sizeof memory-1-i]==0xa5);
 draw("Not charging\n",true,false,1000);
 draw("Not charging\n",true,true,1000);
 rectangle(480,359,1,1,0xffffff); /* out of bounds ignored */
 for(unsigned i=0;i<8;i++)assert(memory[i]==0xa5 && memory[sizeof memory-1-i]==0xa5);
}
''')


class OfflineChargingScreen(unittest.TestCase):
    PRELUDE = '#define main unused_rescue_main\n#include "' + str(ROOT / 'tools/production/offline-charge.c') + '"\n#undef main\n'

    def test_ring_sweeps_while_charging_is_full_when_charged_and_dark_when_paused(self):
        run_c(self.PRELUDE + r'''
#include <assert.h>
static unsigned char memory[480*360*4];
static unsigned at(unsigned x,unsigned y){unsigned o=(y*480+x)*4;return memory[o+2]<<16|memory[o+1]<<8|memory[o];}
static int green(unsigned x,unsigned y){unsigned v=at(x,y);return ((v>>8)&255)>((v>>16)&255)+40;} /* green clearly above red */
int main(void){
 pixels=memory;fix.line_length=480*4;fix.smem_len=sizeof memory;
 var.xres=480;var.yres=360;var.bits_per_pixel=32;
 var.red=(struct fb_bitfield){16,8,0};var.green=(struct fb_bitfield){8,8,0};var.blue=(struct fb_bitfield){0,8,0};
 unsigned cx=(unsigned)CHG_CX,cy=(unsigned)CHG_CY,r=(unsigned)((CHG_OUTER+CHG_INNER)/2);
 /* One twelfth: only the first 30 degrees after 12 o'clock are lit. */
 draw("Charging\n",false,false,0);
 assert(green(cx+r/4,cy-r+1) && !green(cx-r+1,cy) && !green(cx,cy+r-1) && !green(cx+r-1,cy));
 /* Four twelfths: the right side too, never the bottom or left. */
 draw("Charging\n",false,false,1500);
 assert(green(cx+r-1,cy) && !green(cx,cy+r-1) && !green(cx-r+1,cy));
 /* Twelve twelfths close the ring; the sweep then restarts. */
 draw("Charging\n",false,false,5500);
 assert(green(cx,cy+r-1) && green(cx-r+1,cy) && green(cx,cy-r+1));
 draw("Charging\n",false,false,6000);
 assert(green(cx+r/4,cy-r+1) && !green(cx-r+1,cy));
 draw("Full\n",false,false,123);
 for(unsigned a=0;a<4;a++){unsigned xs[4]={cx,cx+r-1,cx,cx-r+1},ys[4]={cy-r+1,cy,cy+r-1,cy};assert(green(xs[a],ys[a]));}
 draw("Not charging\n",false,false,500);
 for(unsigned a=0;a<4;a++){unsigned xs[4]={cx,cx+r-1,cx,cx-r+1},ys[4]={cy-r+1,cy,cy+r-1,cy};assert(!green(xs[a],ys[a]));}
 /* Text is drawn (the title region is no longer background). */
 unsigned changed=0;for(unsigned y=205;y<245;y++)for(unsigned x=140;x<340;x++)changed+=at(x,y)!=CHG_BG;
 assert(changed>200);
}
''')

    def test_copy_makes_no_charge_claim_and_hints_follow_the_button(self):
        text = (ROOT / 'tools/graphics/offline-charge-art.h').read_text()
        assert '#define CHG_HINT' in text
        source = (ROOT / 'tools/production/offline-charge.c').read_text()
        self.assertIn('never a claimed state of charge', source)

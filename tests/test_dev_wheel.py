"""Execute the real APT32F handler and stock sleep sequence against fixtures."""
import re, subprocess, tempfile, unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
SHIM=r'''
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
typedef unsigned char u8;
typedef int irqreturn_t;
#define IRQ_HANDLED 1
#define I2C_M_RD 1
#define KEY_UP 103
#define KEY_PAGEUP 104
#define KEY_DOWN 108
#define KEY_PAGEDOWN 109
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define dev_warn_ratelimited(...) ((void)0)
struct i2c_client {unsigned addr;void *adapter;int dev;};
struct input_dev {int unused;};
struct gpio_desc {int value;};
struct i2c_msg {unsigned addr,flags,len;u8 *buf;};
static unsigned calls,events,syncs,last_key;static int result=2;
static u8 packet[9]={0xaa,0x55,3,0,1,0,0,0,0};
static unsigned transcript_len,mode_calls;static char transcript[64][24];static int mode_result[8];
static void note(const char *s) {assert(transcript_len<64);strcpy(transcript[transcript_len++],s);}
static int i2c_transfer(void *adap,struct i2c_msg *m,int count) {
 (void)adap;calls++;assert(count==2 && m[0].addr==0x51 && m[1].addr==0x51);
 assert(!m[0].flags && m[0].len==1 && m[0].buf[0]==0);
 assert(m[1].flags==I2C_M_RD && m[1].len==9);
 memcpy(m[1].buf,packet,9);return result;
}
static int i2c_smbus_write_byte_data(struct i2c_client *c,u8 reg,u8 value) {
 char s[24];assert(c->addr==0x51);snprintf(s,sizeof s,"w%u=%u",reg,value);note(s);
 return mode_calls<8 ? mode_result[mode_calls++] : 0;
}
static int gpiod_set_value_cansleep(struct gpio_desc *d,int value) {d->value=value;note(value?"reset":"run");return 0;}
static void msleep(unsigned ms) {char s[24];snprintf(s,sizeof s,"sleep%u",ms);note(s);}
static void input_report_key(struct input_dev *dev,unsigned key,int value) {
 (void)dev;assert(value==(events%2==0));events++;last_key=key;
}
static void input_sync(struct input_dev *dev) {(void)dev;syncs++;}
'''


def core():
    source=(ROOT/'kernel/platform/apt32f-wheel.c').read_text()
    return source[source.index('#define APT32F_FRAME_LEN'):source.index('static int apt32f_probe')]


def run(main):
    with tempfile.TemporaryDirectory() as tmp:
        p=Path(tmp);(p/'test.c').write_text(SHIM+core()+'int main(void) {'+main+'}')
        subprocess.run(['clang','-Wall','-Wextra','-Werror','-Wno-unknown-warning-option','-Wno-unused-but-set-global','-Wno-unused-parameter','-Wno-unused-function',
                        '-O2',str(p/'test.c'),'-o',str(p/'test')],check=True)
        return subprocess.run([str(p/'test')],check=True,capture_output=True,text=True).stdout


class WheelRegression(unittest.TestCase):
    def test_register_zero_restart_and_bounded_failure(self):
        run(r'''
 struct i2c_client client={.addr=0x51};struct input_dev input={0};
 struct apt32f_wheel wheel={.client=&client,.input=&input};
 result=-ETIMEDOUT;
 for(unsigned i=0;i<8;i++) assert(apt32f_irq(55,&wheel)==IRQ_HANDLED);
 assert(calls==8 && !events); /* no handler retries or false scroll */
 result=1;apt32f_irq(55,&wheel);assert(!events);
 result=2;
 for(unsigned code=1;code<=4;code++){packet[4]=code;apt32f_irq(55,&wheel);assert(last_key==apt32f_scroll_keys[code]);}
 assert(events==8 && syncs==8);
 packet[0]=0;apt32f_irq(55,&wheel);assert(events==8);packet[0]=0xaa;
 packet[1]=0;apt32f_irq(55,&wheel);assert(events==8);packet[1]=0x55;
 packet[2]=1;apt32f_irq(55,&wheel);assert(events==8);packet[2]=3;
 packet[4]=9;apt32f_irq(55,&wheel);assert(events==8);
 return 0;
''')


class WheelSleep(unittest.TestCase):
    """Stock tpd_suspend/tpd_resume ordering (Y2 FM kernel), from the real source."""
    def transcript(self, body):
        return run(body+r' for(unsigned i=0;i<transcript_len;i++) printf("%s\n",transcript[i]); return 0;').split()

    def test_firmware_1_uses_sleep_command_and_stock_reset_pulse_on_wake(self):
        # The physical Y2 reports reg6 = 0x01 (read 2026-10-11).
        steps=self.transcript(r'''
 struct i2c_client c={.addr=0x51};struct gpio_desc g={0};
 struct apt32f_wheel w={.client=&c,.reset=&g,.version=1,.sleep_enabled=true};
 assert(!apt32f_enter_sleep(&w) && w.state==APT32F_I2C_SLEEP && w.sleeps==1 && !g.value);
 assert(!apt32f_leave_sleep(&w) && w.state==APT32F_AWAKE && w.wakes==1 && !g.value);
''')
        self.assertEqual(steps,['w5=1','run','sleep10','reset','sleep20','run','w5=0'])

    def test_other_firmware_holds_reset_and_wake_retries_while_booting(self):
        steps=self.transcript(r'''
 struct i2c_client c={.addr=0x51};struct gpio_desc g={0};
 struct apt32f_wheel w={.client=&c,.reset=&g,.version=20,.sleep_enabled=true};
 mode_result[0]=-6;mode_result[1]=-6;
 assert(!apt32f_enter_sleep(&w) && w.state==APT32F_RESET_HELD && g.value==1);
 assert(!apt32f_leave_sleep(&w) && w.wake_retries==2 && w.wakes==1 && !g.value);
''')
        self.assertEqual(steps,['reset','run','sleep10','reset','sleep20','run',
                                'w5=0','sleep20','w5=0','sleep20','w5=0'])

    def test_unreadable_firmware_without_reset_owner_does_nothing(self):
        steps=self.transcript(r'''
 struct i2c_client c={.addr=0x51};
 struct apt32f_wheel w={.client=&c,.version=-110,.sleep_enabled=true};
 assert(!apt32f_enter_sleep(&w) && w.state==APT32F_AWAKE && !w.sleeps && !apt32f_leave_sleep(&w));
''')
        self.assertEqual(steps,[])

    def test_disabled_or_failed_sleep_never_blocks_and_wake_is_bounded(self):
        steps=self.transcript(r'''
 struct i2c_client c={.addr=0x51};struct gpio_desc g={0};
 struct apt32f_wheel w={.client=&c,.reset=&g,.version=1};
 assert(!apt32f_enter_sleep(&w) && w.state==APT32F_AWAKE && !apt32f_leave_sleep(&w));
 w.sleep_enabled=true;mode_result[0]=-110;
 assert(apt32f_enter_sleep(&w)==-110 && w.state==APT32F_AWAKE && w.failures==1 && !apt32f_leave_sleep(&w));
 for(unsigned i=2;i<8;i++) mode_result[i]=-110;
 assert(!apt32f_enter_sleep(&w));
 assert(apt32f_leave_sleep(&w)==-110 && w.failures==2 && w.state==APT32F_AWAKE);
''')
        self.assertEqual(steps[:2],['w5=1','w5=1'])
        self.assertEqual(steps.count('w5=0'),5)
        self.assertEqual(steps.count('sleep20'),1+5)


if __name__ == '__main__':
    unittest.main()

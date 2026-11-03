"""Exercise the production USB worker/status failure path without hardware."""
from pathlib import Path
import subprocess
import tempfile
import unittest
from test_audio import function
from test_power import run_c

ROOT = Path(__file__).resolve().parents[1]


class UsbFailure(unittest.TestCase):
    def test_poll_failure_survives_teardown(self):
        adapter = (ROOT / 'kernel/platform/usb.c').read_text()
        worker = adapter[adapter.index('static void y2_usb_worker(struct work_struct *work)\n{'):
                         adapter.index('static void y2_usb_begin(void)\n{')]
        status = adapter[adapter.index('static ssize_t y2_usb_status(char __user *buf)\n{'):adapter.index('static ssize_t status_show')]
        source = r'''
#include <assert.h>
#include <string.h>
#include <stddef.h>
#include <errno.h>
#include <stdbool.h>
#include "live.h"
#include "fault.h"
#define __user
#define READ_ONCE(x) (x)
#define WRITE_ONCE(x,v) ((x)=(v))
#define ENODEV 19
#define EFAULT 14
#define MUSB_DEVCTL 0x60
#define USB_STATE_CONFIGURED 7
typedef long ssize_t;
struct work_struct { int unused; };
struct usb_role_switch { int unused; };
enum usb_role { USB_ROLE_NONE, USB_ROLE_HOST, USB_ROLE_DEVICE };
static enum usb_role y2_requested_role = USB_ROLE_DEVICE;
struct musb { unsigned char *mregs; struct {unsigned state;} g; };
static struct musb *y2_musb;
static struct y2_usb_live y2_live;
static struct y2_pwrap_snapshot next_power,y2_usb_supply;
static void *y2_usb_pmic;
static int y2_usb_child,y2_usb_finished,y2_usb_failure_lock,y2_usb_work;
static void *y2_usb_phy;
static bool y2_usb_clock_enabled;
static bool y2_usb_sleeping,y2_usb_monitor_parked;
static void y2_usb_kick(void){}
static unsigned childless_gates;
static int clock_error;
static unsigned y2_usb_detached,detached,reconnected;
static int y2_usb_lifecycle;
static int classified=1;
static void mutex_lock(int *l){assert(!*l);*l=1;}
static void mutex_unlock(int *l){assert(*l);*l=0;}
static int y2_usb_bus_gate(void){assert(y2_usb_lifecycle && !y2_usb_child);++childless_gates;
 if(clock_error)return clock_error;y2_usb_clock_enabled=false;return 0;}
static void y2_usb_source_invalidate(void){classified=0;}
static int y2_usb_data_permitted(void){return classified;}
static int reconnect_rc;
static unsigned probes,scheduled,finished,registered;
static unsigned msecs_to_jiffies(unsigned ms) {return ms;}
static void spin_lock(int *lock) {assert(!*lock);*lock=1;}
static void spin_unlock(int *lock) {assert(*lock);*lock=0;}
/* Inject the canonical serialized PMIC provider's public result. */
static int y2_pmic_snapshot(struct y2_pwrap_snapshot *out)
{++probes;*out=next_power;return out->result;}
static struct y2_usb_tolerance y2_tolerance;
static bool y2_usb_pm_held;
static unsigned last_reason;
typedef unsigned long ulong_flags;
#define spin_lock_irqsave(l,f) ((void)(f))
#define spin_unlock_irqrestore(l,f) ((void)(f))
static void y2_usb_fail_at(struct musb *m,unsigned reason,int rc,bool registers)
{(void)m;(void)registers;if(!y2_live.result){y2_live.result=rc;last_reason=reason;}}
static void y2_usb_phase(unsigned stage) {y2_live.stage=stage;}
static int y2_usb_register(void) {++registered;return 0;}
static unsigned readb(void *p) {(void)p;return 0x98;}
static void y2_usb_detach(void) {++detached;y2_usb_detached=1;y2_usb_phase(Y2_USB_DETACHED);}
static int y2_usb_reconnect(bool *wrote) {++reconnected;*wrote=false;if(!reconnect_rc)y2_usb_detached=0;return reconnect_rc;}
static int freezable_queue;
#define system_freezable_wq (&freezable_queue)
static void queue_delayed_work(int *queue,int *work,unsigned delay)
{assert(queue==system_freezable_wq && work==&y2_usb_work && delay==250);++scheduled;}
static void y2_usb_finish(void) {++finished;y2_usb_finished=1;y2_usb_pmic=NULL;}
static int copy_to_user(void *dest,const void *src,unsigned n)
{assert(!y2_usb_failure_lock);memcpy(dest,src,n);return 0;}
'''
        source += worker + status + function(adapter, 'y2_role_set') + r'''
int main(void) {
    assert(sizeof(struct y2_usb_live)==88);
    /* No data cable since boot: no child can ever invoke runtime suspend.
     * A quiesced parent must release its clock, without registering a gadget.
     * Repeated monitor ticks must not add clock references or repeat gates. */
    y2_live=(struct y2_usb_live){.magic=Y2_USB_LIVE_MAGIC};
    next_power=(struct y2_pwrap_snapshot){.magic=Y2_PWRAP_MAGIC,.valid=7,.vusb=0xc000,.chrdet=1};
    y2_usb_phy=&next_power;y2_usb_clock_enabled=true;
    y2_usb_worker(NULL);
    assert(!y2_usb_child && !registered && !y2_usb_clock_enabled && childless_gates==1);
    assert(y2_usb_monitor_parked && !scheduled);
    for(unsigned n=0;n<100;++n)y2_usb_worker(NULL);
    assert(childless_gates==1 && !finished);
    /* A verified gate error is terminal, not a successful C3 admission. */
    y2_usb_clock_enabled=true;clock_error=-EIO;
    y2_usb_worker(NULL);
    assert(finished==1 && y2_live.result==-EIO && last_reason==Y2_USB_REASON_PREFLIGHT);
    y2_usb_phy=NULL;y2_usb_clock_enabled=false;clock_error=0;
    for(unsigned fault=0;fault<5;++fault) {
        y2_live=(struct y2_usb_live){.magic=Y2_USB_LIVE_MAGIC,.stage=Y2_USB_ATTACH,.devctl=0x100};
        next_power=(struct y2_pwrap_snapshot){.magic=Y2_PWRAP_MAGIC,.valid=7,
            .wrap=1,.arb=0x1ff,.channel=1,.init=1,.before=0x00300001,.after=0x00300001,
            .cid=0x2023,.vusb=0xc000,.chrdet=1};
        y2_usb_pmic=&next_power;y2_usb_finished=0;
        probes=scheduled=finished=registered=0;y2_tolerance=(struct y2_usb_tolerance){0};
        struct y2_usb_live status;
        assert(y2_usb_status((char*)&status)==sizeof(status));
        assert(!status.power_failure.magic); /* no invented failure before polling */
        if(fault==1) {next_power.result=-16;next_power.valid=0;next_power.before=next_power.after=0x00200001;}
        if(fault==2) {next_power.result=-16;next_power.valid=0;next_power.before=next_power.after=0x00360001;}
        if(fault==3) {next_power.result=-110;next_power.valid=3;next_power.after=0x00340023;}
        if(fault==4) next_power.vusb=0x4000;
        /* Fix02: a PMIC/PWRAP observation failure is retried; only a
         * persistent one (four polls, one second) is terminal. */
        for(unsigned poll=1;fault && poll<Y2_USB_MONITOR_TOLERANCE;++poll) {
            y2_usb_worker(NULL);
            assert(!finished && scheduled==poll && y2_tolerance.monitor_transients==poll && !y2_live.result);
        }
        probes=scheduled=0;
        y2_usb_worker(NULL);
        assert(probes==1 && !registered && !y2_usb_lifecycle);
        assert(y2_usb_status((char*)&status)==sizeof(status));
        assert(status.magic==Y2_USB_LIVE_MAGIC && status.stage==Y2_USB_ATTACH);
        assert(status.polls==(fault ? Y2_USB_MONITOR_TOLERANCE : 1));
        if(fault) assert(last_reason==Y2_USB_REASON_SUPPLY_MONITOR);
        if(!fault) {
            assert(!finished && !scheduled && y2_usb_monitor_parked && !status.result && status.chrdet==1);
            assert(!status.power_failure.magic);
        } else {
            assert(finished==1 && !scheduled && !y2_usb_pmic);
            assert(status.chrdet==0x10000 && status.result==(fault==4 ? -ENODEV : next_power.result));
            assert(!memcmp(&status.power_failure,&next_power,sizeof(next_power)));
            y2_usb_worker(NULL); /* teardown is terminal; do not probe/retry */
            assert(probes==1 && finished==1);
            assert(y2_usb_status((char*)&status)==sizeof(status));
            assert(!memcmp(&status.power_failure,&next_power,sizeof(next_power)));
        }
    }
    /* Persistent attach/detach/reconnect; real power faults remain terminal. */
    for(unsigned fault=0;fault<2;++fault) {
        unsigned char regs[256]={0};struct musb musb={.mregs=regs,.g={.state=7}};
        y2_live=(struct y2_usb_live){.magic=Y2_USB_LIVE_MAGIC};
        next_power=(struct y2_pwrap_snapshot){.magic=Y2_PWRAP_MAGIC,.valid=7,.vusb=0xc000,.chrdet=0x21};
        y2_usb_pmic=&next_power;y2_usb_child=1;y2_musb=&musb;y2_usb_finished=0;
        y2_usb_detached=detached=reconnected=0;finished=0;y2_tolerance=(struct y2_usb_tolerance){0};
        reconnect_rc=fault ? -19 : 0;classified=1;
        y2_usb_worker(NULL);assert(y2_live.stage==Y2_USB_CONFIGURED);
        next_power.chrdet=1;y2_usb_worker(NULL);
        assert(detached==1 && !finished && y2_live.stage==Y2_USB_DETACHED);
        for(unsigned i=0;i<20;++i) y2_usb_worker(NULL);
        assert(detached==1 && !reconnected && !finished); /* no repeated teardown */
        next_power.chrdet=0x21;classified=1;y2_usb_worker(NULL);
        if(fault) {
            /* Write-free reconnect refusals retry for ten seconds, then end. */
            assert(!finished && reconnected==1 && y2_tolerance.reconnect_retries==1);
            for(unsigned i=2;i<Y2_USB_RECONNECT_ATTEMPTS;++i) {y2_usb_worker(NULL);assert(!finished);}
            y2_usb_worker(NULL);
            assert(finished==1 && reconnected==Y2_USB_RECONNECT_ATTEMPTS && last_reason==Y2_USB_REASON_RECONNECT);
            continue;
        }
        assert(reconnected==1 && !y2_usb_detached && !finished && y2_live.stage==Y2_USB_CONFIGURED);
        for(unsigned i=0;i<100;++i) {
            next_power.chrdet=1;y2_usb_worker(NULL);
            for(unsigned idle=0;idle<1300;++idle) y2_usb_worker(NULL);
            assert(!finished && detached==i+2 && reconnected==i+1);
            next_power.chrdet=0x21;classified=1;y2_usb_worker(NULL);
            assert(!finished && reconnected==i+2 && y2_live.stage==Y2_USB_CONFIGURED);
        }
        unsigned connections=reconnected,detachments=detached;
        assert(y2_role_set(NULL,USB_ROLE_HOST)==-EACCES);
        assert(y2_requested_role==USB_ROLE_DEVICE && detached==detachments);
        assert(!y2_role_set(NULL,USB_ROLE_NONE) && detached==detachments+1);
        for(unsigned i=0;i<20;i++) y2_usb_worker(NULL);
        assert(reconnected==connections && y2_usb_detached);
        assert(!y2_role_set(NULL,USB_ROLE_DEVICE));
        y2_usb_worker(NULL);
        assert(reconnected==connections+1 && !y2_usb_detached);
        y2_live.result=-EIO;
        assert(y2_role_set(NULL,USB_ROLE_NONE)==-EIO && y2_requested_role==USB_ROLE_DEVICE);
    }
}
'''
        with tempfile.TemporaryDirectory() as d:
            path = Path(d)
            (path / 'test.c').write_text(source)
            subprocess.run(['clang', '-O2', '-Wall', '-Wextra', '-Werror','-Wno-unknown-warning-option','-Wno-unused-but-set-global', '-Wno-unused-parameter',
                            '-I' + str(ROOT / 'kernel/usb'), str(path / 'test.c'),
                            '-o', str(path / 'test')], check=True)
            subprocess.run([str(path / 'test')], check=True)

    def test_cable_events_wake_detached_worker_without_battery_polling(self):
        source = (ROOT / 'kernel/platform/usb.c').read_text()
        run_c(r'''
#include <assert.h>
#include <stdbool.h>
#include <string.h>
#define READ_ONCE(x) (x)
#define NOTIFY_OK 1
#define PSY_EVENT_PROP_CHANGED 1
struct notifier_block { int unused; };
struct power_supply_desc { const char *name; };
struct power_supply { const struct power_supply_desc *desc; };
static bool y2_usb_started,y2_usb_finished,y2_usb_sleeping;
static unsigned kicked;
static int queue,work;
#define system_freezable_wq (&queue)
#define y2_usb_work work
static void mod_delayed_work(int *q,int *w,unsigned delay) {
 assert(q==&queue && w==&work && !delay);kicked++;
}
''' + function(source, 'y2_usb_kick') + function(source, 'y2_usb_presence_changed') + r'''
int main(void) {
 const struct power_supply_desc cable={"y2-usb-presence"},battery={"BAT0"},budget={"y2-usb-input"};
 struct power_supply supply={&cable};
 y2_usb_presence_changed(0,PSY_EVENT_PROP_CHANGED,&supply);assert(!kicked);
 y2_usb_started=true;
 y2_usb_presence_changed(0,0,&supply);assert(!kicked);
 y2_usb_presence_changed(0,PSY_EVENT_PROP_CHANGED,&supply);assert(kicked==1);
 supply.desc=&battery;y2_usb_presence_changed(0,PSY_EVENT_PROP_CHANGED,&supply);
 supply.desc=&budget;y2_usb_presence_changed(0,PSY_EVENT_PROP_CHANGED,&supply);assert(kicked==1);
 supply.desc=&cable;y2_usb_sleeping=true;
 y2_usb_presence_changed(0,PSY_EVENT_PROP_CHANGED,&supply);assert(kicked==1);
 y2_usb_sleeping=false;y2_usb_kick();assert(kicked==2);
 y2_usb_finished=true;
 y2_usb_presence_changed(0,PSY_EVENT_PROP_CHANGED,&supply);assert(kicked==2);
}
''')

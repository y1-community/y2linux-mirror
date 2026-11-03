"""The retained replay trace is limited to system sleep on the exact Y2 devices."""
from pathlib import Path
import unittest
from test_audio import function
from test_power import run_c

ROOT = Path(__file__).resolve().parents[1]


class ReplayTrace(unittest.TestCase):
    def test_runtime_other_devices_and_other_boards_never_log(self):
        source = (ROOT/'kernel/platform/pm-replay-trace.h').read_text()
        run_c(r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#define IS_ENABLED(c) (true)
#define PM_SUSPEND_ON 0
static int pm_suspend_target_state;
struct device { const char *of_node; };
static bool board;
static unsigned notes;
static int error;
static bool of_machine_is_compatible(const char *s){assert(!strcmp(s,"innioasis,y2"));return board;}
static bool of_device_is_compatible(const char *node,const char *s){return node && !strcmp(node,s);}
static void y2_pm_note(const char *s,int ret){assert(!strcmp(s,"phase"));notes++;error=ret;}
''' + function(source, 'y2_pm_replay_note') + r'''
int main(void){
 const char *nodes[]={"cirrus,cs43131","mediatek,mt6582-i2c","unrelated",NULL};
 for(unsigned b=0;b<2;b++)for(unsigned mode=0;mode<2;mode++)for(unsigned n=0;n<4;n++) {
  board=b;pm_suspend_target_state=mode;struct device d={nodes[n]};notes=0;
  y2_pm_replay_note(&d,"phase",-110);
  assert(notes==(b && mode && n<2));if(notes)assert(error==-110);
 }
 notes=0;y2_pm_replay_note(NULL,"phase",0);assert(!notes);
}
''')

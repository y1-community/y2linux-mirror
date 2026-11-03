python3 - <<'PY'
import os,time,glob
def snap():
    out={}
    for d in glob.glob('/proc/[0-9]*'):
        try:
            pid=int(d.split('/')[-1]); comm=open(d+'/comm').read().strip()
            for t in os.listdir(d+'/task'):
                td=f'{d}/task/{t}'
                st=open(td+'/stat').read().rsplit(')',1)[1].split()
                s=dict(l.split(':',1) for l in open(td+'/status').read().splitlines() if ':' in l)
                out[(pid,int(t),comm)]=(int(st[11])+int(st[12]),int(s['voluntary_ctxt_switches'])+int(s['nonvoluntary_ctxt_switches']))
        except Exception: pass
    return out
def cs():
    return int([l for l in open('/proc/stat') if l.startswith('ctxt')][0].split()[1]), sum(int(x) for x in [l for l in open('/proc/stat') if l.startswith('intr')][0].split()[1:2])
a=snap(); ca=cs(); t=time.monotonic(); time.sleep(120); b=snap(); cb=cs(); dt=time.monotonic()-t
rows=[]
for k,v in b.items():
    if k in a:
        rows.append(((v[1]-a[k][1])/dt,(v[0]-a[k][0])/dt*100/os.sysconf('SC_CLK_TCK'),k))
print('window %.1fs  system ctxt/s %.1f  intr/s %.1f'%(dt,(cb[0]-ca[0])/dt,(cb[1]-ca[1])/dt))
for r in sorted(rows,reverse=True)[:14]: print('%7.2f ctx/s %5.2f%% cpu pid=%d tid=%d %s'%(r[0],r[1],*r[2][:3]))
print('--- named')
for r in sorted(rows,reverse=True):
    if r[2][2] in ('python3','wpa_supplicant','bluealsad','y2-bt-reconnect','y2-calibration','reborn','y2-platform') and r[0]>0.05: print('%7.2f ctx/s %5.2f%% pid=%d %s'%(r[0],r[1],r[2][0],r[2][2]))
PY
echo "wifi irq: $(grep -E 'y2-wifi' /proc/interrupts)"

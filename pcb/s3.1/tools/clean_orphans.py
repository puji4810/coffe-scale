import re, json, subprocess

F='s3.1.kicad_pcb'
ORIG=open(F).read()

def via_spans(text):
    out=[]
    for m in re.finditer(r'\(via(?=[\s(])',text):
        d,i=0,m.start()
        while i<len(text):
            if text[i]=='(':d+=1
            elif text[i]==')':
                d-=1
                if d==0:break
            i+=1
        blk=text[m.start():i+1]
        am=re.search(r'\(at ([\d.-]+) ([\d.-]+)\)',blk)
        nm=re.search(r'\(net "([^"]+)"',blk)
        if am and nm:out.append((m.start(),i+1,float(am.group(1)),float(am.group(2)),nm.group(1)))
    return out

VIAS=via_spans(ORIG)

def remove(idxs):
    text=ORIG
    for i in sorted(idxs,key=lambda i:-VIAS[i][0]):
        s,e=VIAS[i][:2];text=text[:s]+text[e:]
    return text

def drc_unconn(text):
    open('/tmp/_try.kicad_pcb','w').write(text)
    subprocess.run(['kicad-cli','pcb','drc','--format','report','--severity-all','--refill-zones','-o','/tmp/_try-drc.txt','/tmp/_try.kicad_pcb'],capture_output=True)
    try:r=open('/tmp/_try-drc.txt').read()
    except:return 999
    m=re.search(r'Found (\d+) unconnected',r)
    return int(m.group(1)) if m else 999

removed=set()
cands=[i for i,v in enumerate(VIAS) if v[4]=='GND']

def process(idxs):
    global removed
    if not idxs:return
    if drc_unconn(remove(removed|set(idxs)))==0:
        removed|=set(idxs);print('OK batch',len(idxs),flush=True);return
    if len(idxs)==1:
        s,e,x,y,n=VIAS[idxs[0]];print(f'KEEP ({x:.2f},{y:.2f})',flush=True);return
    h=len(idxs)//2
    process(idxs[:h]);process(idxs[h:])

B=8
for i in range(0,len(cands),B):
    process(cands[i:i+B])
print('removed:',len(removed),'of',len(cands))
open(F,'w').write(remove(removed))

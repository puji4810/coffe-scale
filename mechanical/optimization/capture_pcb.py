"""Read live PCB, write only optimization/references/pcb-snapshot.json."""
import sys
from pathlib import Path
HERE=Path(__file__).resolve().parent
sys.path.insert(0,str(HERE/'v032_reference'))
import pcb_capture
pcb_capture.HERE=HERE
pcb_capture.SOURCE=HERE.parents[1]/'pcb/scale-adc-s3/scale-adc-s3.kicad_pcb'
if __name__=='__main__':
    d=pcb_capture.capture()
    print(d['sha256'],d['mounting_holes'])

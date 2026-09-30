"""Render actual v032 CAD, reference-inspired support and PCB hole layout."""
import sys,json
from pathlib import Path
import numpy as np
from model import HERE,P,make_parts,wide_plate,pan_cover
sys.path.append(str(HERE.parent))
from render import draw
from build123d import Pos,Color
import matplotlib.pyplot as plt
from matplotlib.patches import Rectangle,Circle,Polygon
BG='#f4f6fa'

def main():
    out=HERE.parent/'exports/v032';out.mkdir(parents=True,exist_ok=True)
    snapshot=json.loads((HERE/'references/pcb-snapshot.json').read_text())
    parts,groups,_=make_parts(snapshot)
    fig=plt.figure(figsize=(16,11),facecolor=BG)
    fig.suptitle('v0.3.2 / 宽板托架 + 三孔 M2 主板安装',x=.05,y=.98,ha='left',fontsize=24,weight='bold')
    fig.text(.05,.935,'保留 v0.3.1 玻璃、屏幕与密封 · 主体 160 × 140 × 53.3 mm · PCB 为只读实板快照',fontsize=13,color='#526477')
    ax=fig.add_subplot(221,facecolor=BG);draw(ax,parts,'01 / 外观沿用 v3；内部更换连续宽板',focus_z=22,scale=100)
    ax=fig.add_subplot(222,facecolor=BG)
    keep={n:s for n,s in parts.items() if n in ('wide_support','pan_cover','moving_spacer','LC1330_ENVELOPE') or n.startswith(('pan_M4_','moving_M6_'))}
    offsets={'pan_cover':30,'wide_support':10,'moving_spacer':5}
    shifted={n:Pos(0,0,offsets.get(n,10 if n.startswith(('pan_M4_','moving_M6_')) else 0))*s for n,s in keep.items()}
    draw(ax,shifted,'02 / 宽板、独立连续盘面、加载端垫块',focus_z=47,scale=91)
    ax=fig.add_subplot(223,facecolor=BG)
    keep={n:s for n,s in parts.items() if n not in ('printed_shell','front_cover_film','front_cover_bond','pan_cover','pan_glass','pan_glass_bond') and groups[n]!='moving'}
    draw(ax,keep,'03 / PCB 仍在梁前偏右；三个实板孔固定',elevation=90,azimuth=-90,focus_z=10,scale=94)
    ax=fig.add_subplot(224,facecolor=BG)
    draw(ax,{'wide_support':parts['wide_support']},'04 / 底面浅铣腔：保留连续板身和加载端实心区',elevation=-40,focus_z=36,scale=80)
    fig.subplots_adjust(left=.035,right=.97,top=.89,bottom=.07,wspace=.05,hspace=.15)
    fig.text(.05,.025,'力矩由载荷和偏置决定；宽板改变结构刚度，不会消除偏心力矩。电子件高度为包络，尚无刚度/精度实测。',fontsize=12,color='#795568')
    fig.savefig(out/'overview.png',dpi=150,facecolor=BG);plt.close(fig)

    fig,axs=plt.subplots(1,2,figsize=(15,8),facecolor=BG)
    ax=axs[0];ax.set_facecolor(BG)
    # Draw actual top face outlines, retaining hole locations.
    shape=parts['wide_support']
    for edge in shape.edges():
        # Tessellation can inflate BRep bounding boxes; use exact curve samples.
        vv=[edge.position_at(float(t)) for t in np.linspace(0,1,70)]
        if all(abs(v.Z-41)<1e-4 for v in vv):
            ax.plot([v.X for v in vv],[v.Y for v in vv],color='#526d80',lw=1)
    ax.add_patch(Rectangle((-80,-43),160,113,fill=False,ls='--',ec='#6b7886',lw=1.5))
    for x,y in P.pan_mounts:ax.plot(x,y,'o',color='#bb7236',ms=5)
    ax.plot(53,15,'+',ms=14,mew=2,color='#b93d4e');ax.plot(0,13.5,'+',ms=12,color='#377e97')
    ax.annotate('加载端 (53,15)',xy=(53,15),xytext=(4,2),arrowprops={'arrowstyle':'->'},fontsize=11)
    ax.annotate('盘面中心',xy=(0,13.5),xytext=(-75,24),arrowprops={'arrowstyle':'->'},fontsize=11)
    ax.text(0,-58,'M4 孔距 118 × 60；孔中心到盘边：左右 21，前 28，后 25 mm',ha='center',fontsize=10)
    ax.set_title('宽板平面：实线为真实 CAD，虚线为盘外形',loc='left',fontsize=14,weight='bold')
    ax.set_aspect('equal');ax.set_xlim(-90,90);ax.set_ylim(-65,82);ax.set_xlabel('X / mm');ax.set_ylabel('Y / mm');ax.grid(alpha=.15)
    ax=axs[1];ax.set_facecolor(BG)
    ax.add_patch(Rectangle((-10,-45),75,40,facecolor='#dbebe2',edgecolor='#46725b',lw=1.5))
    for c in snapshot['components']:
        x0,y0,x1,y1=c['bbox'];x,y=P.pcb_xy(x1,y0)
        ax.add_patch(Rectangle((x,y),x1-x0,y1-y0,fc='#93afa2',ec='#5e7a6d',lw=.4,alpha=.8))
        if c['reference'] in ('U4','U1','J2','J5','J6','J7','J8'):
            ax.text(x+(x1-x0)/2,y+(y1-y0)/2,c['reference'],fontsize=8,ha='center',va='center')
    for h in snapshot['mounting_holes']:
        x,y=P.pcb_xy(*h['at']);ax.add_patch(Circle((x,y),2.7,fill=False,ec='#b56b2b',lw=1.2));ax.add_patch(Circle((x,y),1.1,fc='white',ec='#b56b2b'))
        ax.annotate(f"{h['reference']} ({x:g}, {y:g})",xy=(x,y),xytext=(x-4,y+7 if y>-20 else y-10),fontsize=10,arrowprops={'arrowstyle':'->'},ha='center')
    ax.add_patch(Rectangle((-65,0),130,30,fc='#dcc889',ec='#a98a44',alpha=.7));ax.text(0,13,'LC1330',ha='center',fontsize=12)
    ax.annotate('板底 Z=11；底部空隙 4 mm',xy=(30,-24),xytext=(16,-66),fontsize=11,arrowprops={'arrowstyle':'->'})
    ax.set_title('PCB：实际孔位与器件平面包络',loc='left',fontsize=14,weight='bold')
    ax.set_aspect('equal');ax.set_xlim(-30,80);ax.set_ylim(-73,35);ax.set_xlabel('X / mm');ax.set_ylabel('Y / mm');ax.grid(alpha=.15)
    fig.suptitle('装配尺寸审阅 / 不能从这张图推断四角称重误差',fontsize=20,weight='bold',y=.98)
    fig.subplots_adjust(left=.055,right=.97,top=.88,bottom=.1,wspace=.22)
    fig.savefig(out/'support-and-pcb.png',dpi=160,facecolor=BG);plt.close(fig)
    print(out/'overview.png')

if __name__=='__main__':main()

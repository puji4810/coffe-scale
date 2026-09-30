"""Actual CAD comparison and detailed sections for the paired optimization."""
import sys,json
import numpy as np
from model import HERE,make_parts,box
sys.path.append(str(HERE.parent))
from render import draw
from render_pan import dim
from build123d import Pos
import matplotlib.pyplot as plt
from matplotlib.collections import PolyCollection
BG='#f4f6fa'


def section(ax,parts,y,limits,palette):
    slab=box(220,.05,70,0,y,-10)
    for n,color in palette.items():
        if n not in parts:continue
        cut=parts[n]&slab
        if cut is None or cut.volume<1e-8:continue
        for face in cut.faces():
            if face.normal_at().Y<.99:continue
            vv,tt=face.tessellate(.02,.08)
            p=np.array([[v.X,v.Z] for v in vv])
            ax.add_collection(PolyCollection(p[np.array(tt)],facecolors=color,edgecolors='none'))
    ax.set_xlim(*limits[:2]);ax.set_ylim(*limits[2:]);ax.set_aspect('equal');ax.axis('off')


def main():
    out=HERE.parent/'exports/optimization';out.mkdir(parents=True,exist_ok=True)
    snap=json.loads((HERE/'references/pcb-snapshot.json').read_text())
    models={v:make_parts(v,snap) for v in ('v031','v032')}
    reports={v:json.loads((HERE.parent/'exports'/f'{v}-optimized'/'validation.json').read_text()) for v in models}
    fig=plt.figure(figsize=(16,11),facecolor=BG)
    fig.suptitle('两版同步优化 / 优先保留 0.3.1，以相同底架和主板布局对比',x=.05,y=.98,ha='left',fontsize=22,weight='bold')
    for i,v in enumerate(models):
        p,g,_=models[v];ax=fig.add_subplot(2,2,i+1,facecolor=BG)
        names=('integrated_pan',) if v=='v031' else ('pan_cover','wide_support')
        draw(ax,{n:p[n] for n in names},'0.3.1 / 一体盘：中梁加宽 + 两道横筋' if v=='v031' else '0.3.2 / 大平面承压：凸台底部避空 0.2 mm',
             elevation=-42,focus_z=39,scale=90)
        ax.text(.05,.02,f"可动铝件 {reports[v]['moving_aluminium_g']:.1f} g；可动总成估计 {reports[v]['moving_total_estimate_g']:.1f} g",transform=ax.transAxes,fontsize=11,color='#526477')
    p,g,_=models['v031'];ax=fig.add_subplot(223,facecolor=BG)
    names={'printed_shell','pan_glass','pan_glass_bond','front_cover_film','front_cover_bond','integrated_pan'}
    draw(ax,{n:s for n,s in p.items() if n not in names and g[n]!='moving'},'共同布局 / 实际三个 M2 孔；板下空间 4 mm',elevation=90,azimuth=-90,focus_z=10,scale=95)
    ax=fig.add_subplot(224,facecolor=BG)
    draw(ax,{'lower_frame':p['lower_frame'],'foot_spacer_4':p['foot_spacer_4']},'共同底架 / 一体固定端台阶；脚座嵌入底架 1.5 mm',elevation=40,focus_z=0,scale=92)
    fig.subplots_adjust(left=.035,right=.97,top=.89,bottom=.07,wspace=.06,hspace=.15)
    fig.text(.05,.025,'保留玻璃、独立显示区、USB/底盖密封和外形高度。差异只在上部承重结构；尚未证明整机刚度或精度改善。',fontsize=12,color='#795568')
    fig.savefig(out/'comparison.png',dpi=150,facecolor=BG);plt.close(fig)

    fig,axs=plt.subplots(1,3,figsize=(16,8),facecolor=BG)
    p,_,_=models['v031']
    palette={'printed_shell':'#415872','lower_frame':'#aabac6','bottom_cover':'#53677c','foot_spacer_4':'#d4ac67','foot_seal_4':'#25a6b7','foot_4':'#29394a','foot_M3_4':'#81909e'}
    section(axs[0],p,60,(60,78,-9,8),palette)
    axs[0].set_title('脚座：M3 啮合 2 → 3.5 mm',fontsize=14,weight='bold')
    dim(axs[0],(73.5,0),(73.5,1.5),'嵌入 1.5',(2.1,0))
    axs[0].annotate('盲孔底部余料 1.25',xy=(68,-4.3),xytext=(60,-8.5),arrowprops={'arrowstyle':'->'},fontsize=10)
    palette={'printed_shell':'#415872','integrated_pan':'#aabac6','capture_pin_3':'#d4ac67'}
    section(axs[1],p,-9,(65,81,18,45),palette)
    axs[1].set_title('防抬起：固定支耳不参与正常承重',fontsize=13,weight='bold')
    dim(axs[1],(68,23.5),(68,25),'1.5',(-1,0))
    axs[1].annotate('Ø4 肩台坐在可动凸台上',xy=(71,36),xytext=(65.5,43.5),arrowprops={'arrowstyle':'->'},fontsize=10)
    axs[1].annotate('孔径 Ø4.8：径向空隙 0.4',xy=(73.2,26),xytext=(65,19),arrowprops={'arrowstyle':'->'},fontsize=10)
    p,_,_=models['v032'];palette={'wide_support':'#839dad','pan_cover':'#bdd0d9'}
    section(axs[2],p,-15,(62.5,66.5,35.2,37.5),palette)
    axs[2].set_title('0.3.2 凸台底部局部放大',fontsize=14,weight='bold')
    dim(axs[2],(63,35.8),(63,36),'0.2',(0.4,0))
    axs[2].text(65.45,36.5,'托板',fontsize=11,rotation=90,color='#203148')
    axs[2].text(63.5,36.8,'盘底凸台',fontsize=11,color='#203148')
    fig.suptitle('关键装配截面 / 来自实际 CAD',fontsize=22,weight='bold',y=.97)
    fig.subplots_adjust(left=.04,right=.97,top=.86,bottom=.13,wspace=.2)
    fig.text(.05,.05,'位移和密封几何通过检查；0.8 mm 向下、1.5 mm 抬起、0.4 mm 侧向均为样机起始值，需实测校准。',fontsize=12,color='#795568')
    fig.savefig(out/'details.png',dpi=160,facecolor=BG);plt.close(fig)
    print(out/'comparison.png')

if __name__=='__main__':main()

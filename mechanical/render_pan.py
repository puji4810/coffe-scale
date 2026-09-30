"""Render the v0.3 monolithic pan and actual BRep sections of the hidden gap."""
import json
import numpy as np
import matplotlib.pyplot as plt
from matplotlib.collections import PolyCollection
from build123d import Color
from render import draw,ROOT
from assembly import make_parts,box
from integrated_pan import integrated_pan,load_adapter
from dimensions import D

BG='#f4f6fa'


def dim(ax,a,b,label,text_offset=(0,0)):
    ax.annotate('',b,a,arrowprops={'arrowstyle':'<->','lw':1,'color':'#7b4b2c'})
    ax.text((a[0]+b[0])/2+text_offset[0],(a[1]+b[1])/2+text_offset[1],label,
            ha='center',va='center',fontsize=11,color='#7b4b2c',backgroundcolor=BG)


def section(ax,parts,axis,limits):
    # Project only the planar cut face, retaining the exact BRep void topology.
    slab=box(.1,200,70,0,0,0) if axis=='X' else box(200,.1,70,0,0,0)
    palette={'integrated_pan':'#88aaba','printed_shell':'#3e526b',
             'front_cover_film':'#2c9eb8','front_cover_bond':'#b58657'}
    for name,color in palette.items():
        cut=parts[name]&slab
        if cut is None or cut.volume<1e-8:
            continue
        for face in cut.faces():
            normal=face.normal_at()
            if (normal.X if axis=='X' else normal.Y)<.99:
                continue
            vertices,triangles=face.tessellate(.03,.1)
            v=np.array([[(p.Y if axis=='X' else p.X),p.Z] for p in vertices])
            ax.add_collection(PolyCollection(v[np.array(triangles)],facecolors=color,edgecolors='none'))
    ax.set_xlim(*limits);ax.set_ylim(28,47);ax.set_aspect('equal');ax.axis('off')


def main():
    out=ROOT/'exports/v03'
    data=json.loads((out/'pan-review.json').read_text())
    pan=integrated_pan();pan.color=Color('#88aaba')
    adapter=load_adapter();adapter.color=Color('#bb9b64')
    fig=plt.figure(figsize=(14,7),facecolor=BG)
    fig.suptitle('v0.3 / 一体承重盘：顶部连续无孔，底面带筋与下翻边',fontsize=20,weight='bold',y=.97)
    ax=fig.add_subplot(121,facecolor=BG)
    draw(ax,{'pan':pan},'上面：160 × 113 mm 连续称面',focus_z=39,scale=83)
    ax=fig.add_subplot(122,facecolor=BG)
    draw(ax,{'pan':pan},'下面：减重腔、螺钉盲孔和遮水边',elevation=-37,focus_z=39,scale=83)
    fig.subplots_adjust(left=.025,right=.985,top=.87,bottom=.14,wspace=.02)
    fig.text(.06,.075,f"承重盘约 {data['pan_mass_g']:.1f} g · 小转接板约 {data['adapter_mass_g']:.1f} g · 整机 160 × 140 × {D.total_height} mm",fontsize=13,color='#33485c')
    fig.text(.06,.03,'转接板先用 M6 固定到 LC1330；承重盘用下方 M4 螺钉固定。上表面没有螺钉孔。',fontsize=12,color='#536476')
    fig.savefig(out/'pan-details.png',dpi=160,facecolor=BG);plt.close(fig)

    snapshot=json.loads((ROOT/'references/pcb-snapshot.json').read_text())
    parts,_,_=make_parts(snapshot)
    fig=plt.figure(figsize=(15,9),facecolor=BG)
    fig.suptitle('下沿间隙 / 无接触搭接结构剖面',fontsize=23,weight='bold',y=.97)
    ax=fig.add_subplot(121,facecolor=BG)
    section(ax,parts,'X',(-51,-29))
    ax.set_title('前缘：承重盘盖住固定显示区分界',loc='left',fontsize=14,weight='bold',pad=30)
    dim(ax,(-44,33),(-44,36),'3.0',(-1.3,0))
    dim(ax,(-41,37.2),(-39,37.2),'2.0',(0,-1.0))
    dim(ax,(-38,38.5),(-38,41),'2.5',(1.5,0))
    ax.annotate('连续铝称面',xy=(-35,43),xytext=(-36,46),fontsize=12,arrowprops={'arrowstyle':'->'})
    ax.annotate('固定前面板覆膜',xy=(-47,33),xytext=(-51,29),fontsize=11,color='#17788f',arrowprops={'arrowstyle':'->','color':'#17788f'})
    ax.annotate('下翻边',xy=(-42,39),xytext=(-49,40.5),fontsize=12,arrowprops={'arrowstyle':'->'})
    ax.annotate('固定挡水立边',xy=(-38,35),xytext=(-37,29),fontsize=11,arrowprops={'arrowstyle':'->'})
    ax.annotate('',xy=(-44,34),xytext=(-44,43),arrowprops={'arrowstyle':'->','color':'#278bc2','lw':2})

    ax=fig.add_subplot(122,facecolor=BG)
    section(ax,parts,'Y',(69,86))
    ax.set_title('侧边：遮挡内缝，水沿外侧下落',loc='left',fontsize=14,weight='bold',pad=30)
    dim(ax,(81.5,33),(81.5,36),'3.0',(1,0))
    dim(ax,(76,37),(78,37),'2.0',(0,-1))
    dim(ax,(75,38.5),(75,41),'2.5',(-1.5,0))
    ax.annotate('内腔',xy=(71,35),xytext=(69.5,29),fontsize=11,arrowprops={'arrowstyle':'->'})
    ax.annotate('固定壳体',xy=(79,31),xytext=(80,29),fontsize=11,arrowprops={'arrowstyle':'->'})
    ax.annotate('',xy=(81,30),xytext=(81,44),arrowprops={'arrowstyle':'->','color':'#278bc2','lw':2})
    fig.subplots_adjust(left=.04,right=.96,top=.85,bottom=.2,wspace=.18)
    fig.text(.06,.145,'静态：侧向间隙 2 mm · 下沿开口 3 mm · 立边与盘底间隙 2.5 mm · 竖向遮挡重叠 2.5 mm',fontsize=13,color='#33485c')
    fig.text(.06,.095,'下沉 0.8 mm 后：盘底至立边仍有 1.7 mm，下沿开口仍有 2.2 mm。覆膜仅粘在固定面板上。',fontsize=12,color='#33485c')
    fig.text(.06,.045,'剖面来自实际 CAD；箭头仅示意排水方向。没有跨越活动缝的密封圈，不代表已通过防水测试。',fontsize=12,color='#795568')
    fig.savefig(out/'water-path.png',dpi=160,facecolor=BG);plt.close(fig)
    print(out/'water-path.png')


if __name__=='__main__':
    main()

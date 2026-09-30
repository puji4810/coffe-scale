"""Render actual v0.3.1 CAD: exterior, glass, bottom seals and USB cassette."""
import json
import numpy as np
import matplotlib.pyplot as plt
from matplotlib.collections import PolyCollection
from build123d import Pos
from render import draw
from sealed_assembly import ROOT,S,make_parts,box
from render_pan import dim
BG='#f4f6fa'


def section(ax,parts,slab,normal_axis,limits,palette):
    for name,color in palette.items():
        cut=parts[name]&slab
        if cut is None or cut.volume<1e-8: continue
        for face in cut.faces():
            normal=face.normal_at()
            if (normal.X if normal_axis=='X' else normal.Y)<.99: continue
            vv,tt=face.tessellate(.03,.1)
            pts=np.array([[(v.Y if normal_axis=='X' else v.X),v.Z] for v in vv])
            ax.add_collection(PolyCollection(pts[np.array(tt)],facecolors=color,edgecolors='none'))
    ax.set_xlim(*limits[:2]);ax.set_ylim(*limits[2:]);ax.set_aspect('equal');ax.axis('off')


def main():
    out=ROOT/'exports/v031';out.mkdir(parents=True,exist_ok=True)
    parts,groups,_=make_parts(json.loads((ROOT/'references/pcb-snapshot.json').read_text()))
    fig=plt.figure(figsize=(16,11),facecolor=BG)
    fig.suptitle('v0.3.1 / 保留 v3 布局：玻璃盖板 + Type-C / 底盖密封',x=.05,y=.98,ha='left',fontsize=23,weight='bold')
    fig.text(.05,.935,f'主体 160 × 140 mm · 总高 {S.total_height:.1f} mm · USB 塞盖侧向突出 4 mm · 图为真实 CAD',fontsize=13,color='#526477')
    ax=fig.add_subplot(221,facecolor=BG)
    draw(ax,parts,'01 / 玻璃只覆盖可动秤盘，显示区保持独立',scale=100,focus_z=22)
    ax=fig.add_subplot(222,facecolor=BG)
    subset={n:s for n,s in parts.items() if n in ('integrated_pan','pan_glass','pan_glass_bond','load_adapter')}
    moved={n:Pos(0,0,20 if n=='pan_glass' else 10 if n=='pan_glass_bond' else 0)*s for n,s in subset.items()}
    draw(ax,moved,'02 / 1.1 mm 玻璃 + 0.2 mm 胶层 + 一体铝盘',scale=88,focus_z=46)
    ax=fig.add_subplot(223,facecolor=BG)
    draw(ax,parts,'03 / 底盖、四个密封紧固点和金属脚座',elevation=-48,scale=98,focus_z=15)
    ax=fig.add_subplot(224,facecolor=BG)
    bottom={n:s for n,s in parts.items() if n.startswith(('bottom_','lid_','foot_'))}
    moved={n:Pos(0,0,14 if n=='bottom_perimeter_gasket' else 0)*s for n,s in bottom.items()}
    draw(ax,moved,'04 / 闭合周边密封圈；脚座和螺钉另有密封',elevation=50,scale=96,focus_z=2)
    fig.subplots_adjust(left=.035,right=.97,top=.89,bottom=.07,wspace=.05,hspace=.15)
    fig.text(.05,.025,'压缩密封件为装配包络；USB 组件待实物选型，尚未进行防水、热冲击或称重实测。',fontsize=12,color='#795568')
    fig.savefig(out/'overview.png',dpi=150,facecolor=BG);plt.close(fig)

    fig=plt.figure(figsize=(14,8),facecolor=BG)
    fig.suptitle('底盖密封剖面：外沿可退让排水，内圈连续压紧',fontsize=22,weight='bold',y=.97)
    ax=fig.add_subplot(121,facecolor=BG)
    palette={'printed_shell':'#3e526b','bottom_cover':'#7d91a9','bottom_perimeter_gasket':'#35a7bc','lower_frame':'#b5c5cc'}
    section(ax,parts,box(200,.1,15,0,0,-8),'Y',(72,82,-5,5),palette)
    ax.set_title('侧边：密封槽与外侧排水退让',fontsize=14,loc='left',weight='bold')
    dim(ax,(81,-1),(81,-.2),'0.8',(0.7,0))
    ax.annotate('闭合密封圈',xy=(78,-.6),xytext=(74,4),arrowprops={'arrowstyle':'->'},fontsize=12)
    ax.annotate('外沿退让 0.3 mm',xy=(79.7,-1.15),xytext=(75,-4.5),arrowprops={'arrowstyle':'->'},fontsize=11)
    ax=fig.add_subplot(122,facecolor=BG)
    # Section through right rear foot centre Y=60.
    palette={'bottom_cover':'#7d91a9','foot_spacer_4':'#b5c5cc','foot_seal_4':'#35a7bc','foot_4':'#303d50','lower_frame':'#adbdc6','foot_M3_4':'#7c8a96'}
    section(ax,parts,box(200,.1,20,0,60,-9),'Y',(60,76,-9,8),palette)
    ax.set_title('脚座：金属直接支承底架，穿孔另加密封',fontsize=14,loc='left',weight='bold')
    ax.annotate('金属肩台',xy=(71,-3.75),xytext=(61,-7),arrowprops={'arrowstyle':'->'},fontsize=12)
    ax.annotate('环形面密封',xy=(73,-3.25),xytext=(71,6),arrowprops={'arrowstyle':'->'},fontsize=12)
    fig.subplots_adjust(left=.05,right=.96,top=.87,bottom=.15,wspace=.2)
    fig.text(.06,.075,'周边圈：自由厚度暂定 1.0 mm，装配 0.8 mm；压紧由实体止口控制，材料与压缩率需实物确认。',fontsize=12,color='#465c70')
    fig.text(.06,.025,'外排水退让位于密封圈之外；螺钉头与脚座穿孔均单独密封。密封槽剖面来自实际 CAD。',fontsize=12,color='#795568')
    fig.savefig(out/'bottom-sections.png',dpi=160,facecolor=BG);plt.close(fig)

    fig=plt.figure(figsize=(13,7),facecolor=BG)
    fig.suptitle('Type-C 密封组件：面板垫圈 + 后部封装 + 可拆塞盖',fontsize=21,weight='bold',y=.96)
    usb={n:s for n,s in parts.items() if n.startswith('usb_')}
    # Re-centre USB before the renderer's camera framing.
    centered={n:Pos(-80,-S.usb_y,-S.usb_z)*s for n,s in usb.items()}
    ax=fig.add_subplot(121,facecolor=BG)
    draw(ax,centered,'装配闭合状态（外形预留）',elevation=20,azimuth=-35,scale=21,focus_z=0)
    ax=fig.add_subplot(122,facecolor=BG)
    separated={n:Pos(12 if n=='usb_sealing_cap' else -9 if n=='usb_panel_gasket' else 0,0,0)*s for n,s in centered.items()}
    draw(ax,separated,'拆开：密封圈与插口塞盖',elevation=28,azimuth=-35,scale=26,focus_z=0)
    fig.subplots_adjust(left=.03,right=.97,top=.85,bottom=.13,wspace=.03)
    fig.text(.06,.065,'紧固螺钉位于面板密封圈之外；插口后部要求密封封装，内部用短线接现有 PCB。',fontsize=12,color='#465c70')
    fig.text(.06,.02,'当前不是已选连接器的制造图；开盖插普通线时不宣称防水。接口与垫圈尺寸需按实物复核。',fontsize=12,color='#795568')
    fig.savefig(out/'usb-details.png',dpi=160,facecolor=BG);plt.close(fig)
    print(out/'overview.png')

if __name__=='__main__': main()

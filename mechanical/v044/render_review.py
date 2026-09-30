"""Actual CAD comparison and sections for the stepped front fascia."""
import json
from build123d import Pos, Color
from model import HERE, OUT, make_parts, box, DISPLAY_DROP, BODY_TOP
from render_utils import draw, plt


def section(p, names, clip, shift=(0,0,0)):
    result = {}
    for n in names:
        s = p[n] & clip
        if s is None or s.volume < 1e-7:
            continue
        s = Pos(*shift)*s
        s.color = p[n].color
        result[n] = s
    return result


def main():
    snap = json.loads((HERE/'references/pcb-snapshot.json').read_text())
    p,g,_ = make_parts(snap)
    # Comparison uses a frozen pre-step copy, independent of old directories.
    import raised_reference
    old,og,_ = raised_reference.make_parts(snap)
    fig,axs = plt.subplots(2,2,figsize=(16,11),facecolor='#f4f6fa')
    fig.suptitle('COFFEE SCALE / v0.4.4 平顶外壳 / 显示玻璃嵌平',x=.035,y=.98,ha='left',fontsize=23,weight='bold')
    fig.text(.035,.935,'外壳顶边与前部玻璃同高 34.0 mm（含脚垫） · 秤盘 43.8 mm · 仅活动秤盘凸出',fontsize=12,color='#526477')
    draw(axs[0,0],{n:s for n,s in old.items() if og[n]!='keepout'},'01 / 上一版：仍有独立凸起的显示台座',elevation=18,azimuth=-50,scale=99,focus_z=19)
    draw(axs[0,1],{n:s for n,s in p.items() if g[n]!='keepout'},'02 / v0.4.4：前区玻璃嵌入平顶外壳',elevation=18,azimuth=-50,scale=99,focus_z=19)
    names = ['printed_shell','pan_cover','pan_glass','fascia_glass','fascia_clear_window','TFT_PCB_ENVELOPE',
             'TFT_PANEL_ENVELOPE','lower_frame','LC7012_ENVELOPE','equal_arm_spider','bottom_cover','PCB_ENVELOPE','PCB_J6']
    visible = section(p,names,box(.6,146,48,-7,0,-4),(7,0,0))
    draw(axs[1,0],visible,'03 / 纵剖面：壳体外沿与显示玻璃共面',elevation=0,azimuth=0,scale=79,focus_z=19)
    names = ['printed_shell','fascia_glass','fascia_clear_window','TFT_PCB_ENVELOPE','TFT_PANEL_ENVELOPE','PCB_J6','PCB_ENVELOPE','TFT_HEADER_KEEPOUT']
    visible = section(p,names,box(.6,34,31,-7,-53,8),(7,53,0))
    visible['TFT_HEADER_KEEPOUT'].color = Color('#dfaf72')
    draw(axs[1,1],visible,'04 / 接线朝后：保留完整 6 mm 接线空间',elevation=0,azimuth=0,scale=22,focus_z=25)
    fig.subplots_adjust(left=.03,right=.98,top=.89,bottom=.07,wspace=.04,hspace=.17)
    fig.text(.035,.025,'屏幕模块在平面内转向 180°；PCB 与 J6 名义净距 1.1 mm。实物插头、走线和显示方向须核对；校准结构保留。',fontsize=11,color='#76565b')
    OUT.mkdir(parents=True,exist_ok=True)
    fig.savefig(OUT/'flush-review.png',dpi=160)
    plt.close(fig)
    fig,ax = plt.subplots(figsize=(11,8),facecolor='#f4f6fa')
    visible = {n:s for n,s in p.items() if g[n]!='keepout'}
    from base_model import ring
    for i,x in enumerate((-49,49),1):
        mark = ring(9,9,.45,.005,2,x,-52.5,39.8-DISPLAY_DROP)
        mark.color = Color('#d3dce1')
        visible[f'touch_symbol_{i}'] = mark
    draw(ax,visible,'v0.4.4 / 平顶外壳，仅秤盘凸起',elevation=24,azimuth=-55,scale=99,focus_z=19)
    fig.tight_layout()
    fig.savefig(OUT/'overview.png',dpi=160)
    plt.close(fig)
    print(OUT/'flush-review.png')


if __name__=='__main__':
    main()

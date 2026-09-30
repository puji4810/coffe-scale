"""Actual CAD renders, underside machining outline and section review drawing."""
import json
import numpy as np
import matplotlib.pyplot as plt
from matplotlib.patches import Circle, Rectangle
from build123d import Color
from render import draw, ROOT
from upper_support import ribbed_support, flat_reference
from dimensions import D

BG = '#f4f6fa'


def dim(ax, a, b, label, offset=(0,0)):
    ax.annotate('', b, a, arrowprops={'arrowstyle':'<->','lw':.8,'color':'#536476'})
    ax.text((a[0]+b[0])/2+offset[0], (a[1]+b[1])/2+offset[1], label,
            ha='center', va='center', fontsize=10, color='#203148', backgroundcolor=BG)


def main():
    out = ROOT/'exports/upper-support'
    data = json.loads((out/'comparison.json').read_text())
    old, new = flat_reference(), ribbed_support()
    old.color, new.color = Color('#9aaebd'), Color('#5b9aa2')
    fig = plt.figure(figsize=(14,7), facecolor=BG)
    fig.suptitle('上托架 v0.2：从平板四臂到一体带筋铝托盘', fontsize=21, weight='bold', y=.97)
    ax = fig.add_subplot(121,facecolor=BG)
    draw(ax,{'old':old},'v0.1 / 3 mm 平板四臂',focus_z=33.5,scale=76)
    ax.text(.08,.02,f"铝件估算 {data['flat_3mm_reference']['estimated_mass_g']:.1f} g", transform=ax.transAxes,fontsize=12)
    ax = fig.add_subplot(122,facecolor=BG)
    draw(ax,{'new':new},'v0.2 / 底面：周边筋 + 中梁 + 四个安装台',elevation=-38,focus_z=37,scale=76)
    ax.text(.05,.02,f"134 × 96 × 10 mm · 铝件估算 {data['ribbed_10mm']['estimated_mass_g']:.1f} g",transform=ax.transAxes,fontsize=12)
    fig.subplots_adjust(left=.03,right=.98,top=.87,bottom=.12,wspace=.02)
    fig.text(.06,.06,'以增加厚度和重量换取更深的承力截面；整机高 49 mm。加载端偏心力矩仍然存在。',fontsize=12,color='#536476')
    fig.text(.06,.025,'尺寸模型已检查装配与限位行程；尚未进行有限元分析和样机偏载测试。',fontsize=11,color='#795568')
    fig.savefig(out/'support-comparison.png',dpi=160,facecolor=BG)
    plt.close(fig)

    fig = plt.figure(figsize=(15,9),facecolor=BG)
    grid = fig.add_gridspec(2,2,width_ratios=[1.35,1],height_ratios=[1,1],wspace=.2,hspace=.35)
    ax = fig.add_subplot(grid[:,0],facecolor=BG)
    # True bottom-face edges from BRep, including the filleted pocket profiles.
    for face in new.faces():
        if abs(face.center().Z-D.spider_z) < 1e-5 and abs(face.normal_at().Z) > .99:
            for edge in face.edges():
                points = [edge.position_at(float(t)) for t in np.linspace(0,1,65)]
                ax.plot([p.X for p in points],[p.Y for p in points],color='#276875',lw=1.2)
    for x,y in D.plate_mounts:
        ax.add_patch(Circle((x,y),1.25,fill=False,linestyle='--',color='#a1663f'))
        ax.plot(x,y,'+',color='#a1663f',markersize=5)
    for dy in (-7.5,7.5):
        ax.add_patch(Circle((53,D.cell_y+dy),5.5,fill=False,linestyle='--',color='#a1663f'))
    ax.axhline(D.cell_y,color='#aaa',lw=.5,ls='-.')
    dim(ax,(-67,69),(67,69),'134')
    dim(ax,(-76,-35),(-76,61),'96',offset=(-1,0))
    dim(ax,(-61,-43),(61,-43),'122（秤盘孔列距）')
    ax.annotate('4×M3 盲孔\n从上表面攻牙',xy=(-61,55),xytext=(-25,48),fontsize=10,
                arrowprops={'arrowstyle':'->','color':'#a1663f'},color='#a1663f')
    ax.annotate('2×Ø6.6 通孔\n上面 Ø11 沉孔',xy=(53,22.5),xytext=(-4,32),fontsize=10,
                arrowprops={'arrowstyle':'->','color':'#a1663f'},color='#a1663f')
    ax.text(-58,-16,'减重腔深 7\n内角 R3',fontsize=11,color='#276875')
    ax.set_xlim(-85,78);ax.set_ylim(-53,78);ax.set_aspect('equal');ax.axis('off')
    ax.set_title('底面特征投影（沿用整机 XY 坐标）',fontsize=14,weight='bold',loc='left')

    ax = fig.add_subplot(grid[0,1],facecolor=BG)
    for y,w,h,z in [(-35,96,3,7),(-35,4,7,0),(11,8,7,0),(57,4,7,0)]:
        ax.add_patch(Rectangle((y,z),w,h,facecolor='#5b9aa2',edgecolor='#276875'))
    dim(ax,(67,0),(67,10),'10',offset=(1,0))
    dim(ax,(-35,-3),(61,-3),'96')
    ax.annotate('上层 3 mm',xy=(-12,8.5),xytext=(-20,15),fontsize=11,
                arrowprops={'arrowstyle':'->'})
    ax.annotate('中梁宽 8 mm',xy=(15,4),xytext=(10,-8),fontsize=11,
                arrowprops={'arrowstyle':'->'})
    ax.text(-36,3,'4',fontsize=10,color='#fff')
    ax.set_xlim(-43,75);ax.set_ylim(-11,19);ax.set_aspect('equal');ax.axis('off')
    ax.set_title('A—A 截面：X = −20 mm',fontsize=14,weight='bold',loc='left')

    ax = fig.add_subplot(grid[1,1],facecolor=BG);ax.axis('off')
    details = [
        '材质：6061-T6；上下两面装夹铣削',
        '底部腔深 7；周边筋宽 4；内角 R3',
        'M6：Ø6.6 通孔 + Ø11 沉孔深 6.5',
        'M6×16 圆柱头螺钉，头顶低于托面 0.5',
        '4×M3×0.5：有效牙深 6，底孔 Ø2.5 深 8',
        '秤盘孔距 122×84；M3×8 头沉入盘面 0.5',
        '传感器净空：空载 3，下沉 0.8 后为 2.2',
        '限位初值 0.8：实际设定仍需加载标定',
    ]
    ax.text(0,1,'加工与装配审阅要点',va='top',fontsize=14,weight='bold')
    for i,line in enumerate(details):
        ax.text(0,.86-i*.105,line,va='top',fontsize=11,color='#33465a')
    fig.suptitle('上托架尺寸审阅 / v0.2 · 单位 mm',fontsize=22,weight='bold',y=.97)
    fig.subplots_adjust(top=.86,bottom=.1,left=.05,right=.97)
    fig.text(.05,.035,'概念审阅图，非加工放行图：公差、安装面平面度、表面处理、刀具与实物螺钉需确认。',fontsize=12,color='#795568')
    fig.savefig(out/'support-drawing.png',dpi=160,facecolor=BG)
    plt.close(fig)
    print(out/'support-comparison.png')


if __name__ == '__main__':
    main()

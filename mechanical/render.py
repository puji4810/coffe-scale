"""Render the actual tessellated CAD solids to reviewable PNGs (headless)."""
import json
from pathlib import Path
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from build123d import Pos
import vtk
from vtk.util.numpy_support import vtk_to_numpy
from assembly import make_parts, lower_frame
from dimensions import D

ROOT = Path(__file__).resolve().parent
plt.rcParams.update({'font.family': 'Noto Sans CJK SC', 'font.size': 10,
                     'axes.unicode_minus': False})


def draw(ax, parts, title, exploded=False, elevation=29, azimuth=-61, focus_z=None, scale=None):
    renderer = vtk.vtkRenderer()
    renderer.SetBackground(244/255, 246/255, 250/255)
    for name, original in parts.items():
        s = original
        if exploded:
            delta = (55 if name == 'integrated_pan' else
                     37 if name == 'load_adapter' or name.startswith(('moving_M6','pan_M4')) else
                     18 if name == 'LC1330_ENVELOPE' else 0)
            s = Pos(0, 0, delta) * s
        verts, tris = s.tessellate(0.25, 0.2)
        v = np.array([[p.X, p.Y, p.Z] for p in verts])
        points = vtk.vtkPoints()
        for p in v:
            points.InsertNextPoint(*p)
        cells = vtk.vtkCellArray()
        for tri in tris:
            cells.InsertNextCell(3)
            for index in tri:
                cells.InsertCellPoint(int(index))
        mesh = vtk.vtkPolyData(); mesh.SetPoints(points); mesh.SetPolys(cells)
        mapper = vtk.vtkPolyDataMapper(); mapper.SetInputData(mesh)
        actor = vtk.vtkActor(); actor.SetMapper(mapper)
        c = tuple(original.color)[:3] if original.color else (.6, .7, .8)
        actor.GetProperty().SetColor(*c)
        if original.color:
            actor.GetProperty().SetOpacity(tuple(original.color)[3])
        actor.GetProperty().SetAmbient(.35)
        actor.GetProperty().SetDiffuse(.65)
        renderer.AddActor(actor)
    camera = renderer.GetActiveCamera()
    theta, phi = np.deg2rad([azimuth, elevation])
    target_z = focus_z if focus_z is not None else (47 if exploded else 20)
    camera.SetFocalPoint(0, 0, target_z)
    camera.SetPosition(400*np.cos(phi)*np.cos(theta), 400*np.cos(phi)*np.sin(theta),
                       target_z+400*np.sin(phi))
    camera.SetViewUp(0, 1, 0) if elevation == 90 else camera.SetViewUp(0, 0, 1)
    camera.ParallelProjectionOn(); camera.SetParallelScale(scale or (112 if exploded else 101))
    renderer.ResetCameraClippingRange()
    window = vtk.vtkRenderWindow(); window.SetOffScreenRendering(1)
    window.SetSize(1100, 800); window.AddRenderer(renderer); window.SetMultiSamples(4)
    window.Render()
    capture = vtk.vtkWindowToImageFilter(); capture.SetInput(window)
    capture.SetInputBufferTypeToRGB(); capture.ReadFrontBufferOff(); capture.Update()
    rgb = vtk_to_numpy(capture.GetOutput().GetPointData().GetScalars()).reshape(800, 1100, 3)
    ax.imshow(np.flipud(rgb)); window.Finalize()
    ax.set_axis_off()
    ax.set_title(title, fontsize=15, weight='bold', pad=5, loc='left')


def main():
    snapshot = json.loads((ROOT/'references/pcb-snapshot.json').read_text())
    parts, groups, _ = make_parts(snapshot)
    out = ROOT/'exports'
    out.mkdir(exist_ok=True)
    fig = plt.figure(figsize=(16, 12), facecolor='#f4f6fa')
    fig.suptitle('COFFEE SCALE / LC1330  ·  无孔一体承重盘 v0.3',
                 x=.06, y=.98, ha='left', fontsize=23, weight='bold', color='#203148')
    fig.text(.06, .935, f'{D.width} × {D.depth} × {D.total_height} mm     |     秤面 {D.pan_w} × {D.pan_plan_depth} mm     |     缝隙藏在下沿     |     单位 mm',
             fontsize=13, color='#546477')
    for i in range(4):
        ax = fig.add_subplot(2, 2, i+1, facecolor='#f4f6fa')
        if i == 0:
            draw(ax, parts, '01 / 连续无孔称面，前缘遮住显示区分界')
        elif i == 1:
            internal = {n:s for n,s in parts.items() if n not in ('integrated_pan', 'printed_shell','front_cover_film','front_cover_bond')}
            draw(ax, internal, '02 / 小转接板：上装 M6，下装 M4', elevation=49)
        elif i == 2:
            exploded = {n:s for n,s in parts.items() if n not in ('printed_shell','front_cover_film','front_cover_bond')}
            draw(ax, exploded, '03 / 一体盘、加强筋和下翻边为同一个零件', exploded=True)
        else:
            base = {n:s for n,s in parts.items() if groups[n] != 'moving' and n not in ('printed_shell','front_cover_film','front_cover_bond')}
            draw(ax, base, '04 / 俯视：电池左前，PCB 右前，传感器横放', elevation=90, azimuth=-90)
    fig.subplots_adjust(left=.035, right=.97, top=.89, bottom=.08, wspace=.06, hspace=.15)
    fig.text(.06, .04, '无接触搭接与外排水结构；不是水密封。电子件、覆膜与限位仍需实物验证。',
             fontsize=12, color='#6a5260')
    fig.savefig(out/'overview.png', dpi=150, facecolor=fig.get_facecolor())
    plt.close(fig)
    fig = plt.figure(figsize=(14, 7), facecolor='#f4f6fa')
    for i, kind in enumerate(('rectangle', 'butterfly'), 1):
        ax = fig.add_subplot(1, 2, i, facecolor='#f4f6fa')
        f = lower_frame(kind)
        title = ('矩形减重骨架（默认）' if i == 1 else '偏置四叉臂骨架（对照）')
        draw(ax, {'frame': f}, title, elevation=70, azimuth=-90)
        ax.text(.1, .02, f'4 mm 铝板 · 几何体积估算 {f.volume*0.0027:.1f} g', transform=ax.transAxes)
    fig.suptitle('底盘方案对比：固定端位于左侧，前部缺口避开天线', fontsize=19, weight='bold')
    fig.text(.08, .03, '四叉臂减重效果取决于臂宽和刚度需求；当前未进行有限元或角载挠度测试。', fontsize=12)
    fig.savefig(out/'frame-comparison.png', dpi=150, facecolor=fig.get_facecolor())
    plt.close(fig)
    print(out/'overview.png')


if __name__ == '__main__':
    main()

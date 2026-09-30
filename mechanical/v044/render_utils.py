"""Headless rendering of v0.4.2 CAD solids, not an illustrative reconstruction."""
import json
import numpy as np
import vtk
from vtk.util.numpy_support import vtk_to_numpy
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib.patches import Rectangle, Circle
from build123d import Pos
from model import HERE, OUT, CY, MOUNTS, make_parts
plt.rcParams.update({'font.family':'Noto Sans CJK SC','font.size':10,'axes.unicode_minus':False})
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



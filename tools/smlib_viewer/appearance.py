# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Viewer illumination and material; never changes mesh positions/connectivity."""
from tools.smlib_gui.runtime import np, pv

SURFACE_COLOR = (0.76, 0.79, 0.83)


def configure_scene(plot):
    plot.set_background("#252b34", top="#485363")
    plot.remove_all_lights()
    # Directional camera lights keep shape legible during orbit and on models
    # with different units/scales. The restrained fill leaves a visible gradient
    # across curved faces; a headlight alone makes them look like flat cutouts.
    for position, intensity, color in [
        ((-3, 4, 5), 1.0, "#fff1df"),
        ((4, 1, 2), 0.40, "#dceaff"),
        ((1, 3, -3), 0.30, "#ffffff"),
    ]:
        plot.add_light(pv.Light(position=position, focal_point=(0, 0, 0),
                                light_type="camera light", intensity=intensity, color=color))
    plot.renderer.LightFollowCameraOn()
    plot.enable_anti_aliasing("fxaa")


def style_surface(actor):
    # These are disposable display properties. Source normals, winding, units,
    # geometry and the importer acceptance configuration remain untouched.
    for prop in (actor.GetProperty(), actor.GetBackfaceProperty()):
        if prop is not None:
            prop.SetColor(*SURFACE_COLOR)
            prop.SetLighting(True)
            prop.SetInterpolationToPhong()
            prop.SetAmbient(0.16)
            prop.SetDiffuse(0.80)
            prop.SetSpecular(0.24)
            prop.SetSpecularPower(32)


def verify_model_frame(plot):
    plot.render()
    image = plot.screenshot(return_img=True, transparent_background=False)
    if image.ndim != 3 or image.shape[2] < 3 or not image.size:
        raise ValueError("InvalidModelFramebuffer")
    actors = [(actor, actor.GetVisibility()) for actor in plot.renderer.actors.values()
              if isinstance(actor, pv.Actor)]
    # With this gradient/FXAA pipeline, readback alpha can be opaque even on
    # background pixels. Compare with the same frame minus model actors instead.
    # Axes remain in both frames; sparse parts need not cover the image center.
    # Do not service Qt events while visibility is temporarily changed, and
    # restore every disposable actor on failure. Geometry/camera stay unchanged.
    try:
        for actor, _ in actors:
            actor.SetVisibility(False)
        plot.render()
        background = plot.screenshot(return_img=True, transparent_background=False)
    finally:
        for actor, visible in actors:
            actor.SetVisibility(visible)
        plot.render()
    if image.shape != background.shape or not np.any(image[:, :, :3] != background[:, :, :3]):
        raise ValueError("EmptyModelFramebuffer")

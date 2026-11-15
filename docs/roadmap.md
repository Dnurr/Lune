# Roadmap

What is done, what comes next, and who writes it. Lune splits the work in two:

- **Author**: the rendering and VFX core (shading, lighting, post-processing, simulations).
  These parts are written by hand; Claude reviews them and explains the theory.
- **Claude**: the infrastructure around it (build, UI, file formats, GPU resource plumbing,
  undo, tests, docs). In shaders the author owns, Claude only adds declarations
  (uniform blocks, bindings) and comments.

Every item tagged **Claude** leaves the shader side to the author with the inputs ready:
buffers bound, textures loaded, a `// TODO` where the math goes.

Legend: `[x]` done, `[ ]` to do, `[~]` in progress.

## 1. Editor foundations ✓

- [x] Scene model, entities, component descriptors, automatic Inspector (Claude)
- [x] Selection, undo / redo with merged drags (Claude)
- [x] Glass panels, outliner, command palette, Add menu (Claude)
- [x] `.lune` scene files, OBJ and glTF import (Claude)
- [x] Picking, move / rotate / scale gizmo (Claude)
- [x] Stats overlay, automated tests, documentation (Claude)
- [x] Assets browser (folder tree + contents), resizable panels, Tab to hide them, layout saved between sessions (Claude)
- [x] Free camera (fly with right button + WASD) next to the orbit camera, mode bar in the viewport (Claude)

## 2. HDR and shading (current phase)

### Done

- [x] 16-bit float scene target, display pass, exposure (Claude: plumbing, Author: shader)
- [x] Tonemapping: Reinhard, ACES, AgX (Author)
- [x] Undoable display settings (Claude)
- [x] Material component: roughness, metallic (Claude)
- [x] Cook-Torrance BRDF: GGX, Smith, Schlick (Author)
- [x] Light component, directional and point, lights buffer, viewport icons (Claude)
- [x] Light loop in `mesh.frag` (Author)

### Image-based lighting

- [x] Load equirectangular HDR images (`.hdr`) (Claude)
- [x] Environment component: HDRI, gradient fallback, rotation, intensity, saved in the scene (Claude)
- [x] Cubemap targets, mip chain, precompute passes run when the source changes (Claude)
- [x] Background pass and toggle, grid drawn over it (Claude)
- [x] Equirectangular to cubemap conversion, `equirect_to_cube.frag` (Author)
- [x] Diffuse irradiance convolution, `irradiance.frag` (Author)
- [x] Prefiltered specular cubemap, one roughness level per mip, `prefilter.frag` (Author)
- [x] BRDF integration LUT, `brdf_lut.frag` (Author)
- [x] Split-sum ambient in `mesh.frag`, replacing the constant ambient (Author)
- [x] Environment background with blur, `background.frag` (Author)
- [ ] `.exr` environment images (Claude, with the EXR writer of phase 6)
- [ ] Load large HDRIs off the render thread (Claude)

### Shadows

- [x] Shadow map render target and light-space depth pass, `shadows` / `softness` light properties (Claude)
- [x] Shadows from every light: directional and point (cube faces) atlases, raw depth bindings, `size` property (Claude)
- [x] Shadow sampling and PCF filtering, directional and point lights (Author)
- [x] Contact-hardening soft shadows (PCSS) from the light's size (Author)
- [x] Cascaded shadow maps: splits, per-cascade fitting, texel snapping (Claude)
- [x] Cascade selection in `mesh.frag`, dithered blending across cascade borders (Author)

### Materials

- [ ] Texture loading and caching, sampler bindings per material (Claude)
- [ ] glTF material import: base color, metallic-roughness, normal, emissive (Claude)
- [ ] Texture slots in the Material Inspector (Claude)
- [ ] Texture sampling, normal mapping with tangents (Author; Claude computes the tangents)
- [ ] Emissive color and strength (Claude: property, Author: shader)

### Color management

- [ ] Working and display color spaces in the Display menu (Claude)
- [ ] OpenColorIO integration, as an optional dependency (Claude)
- [ ] View transforms as shaders or a 3D LUT in the display pass (Author)

## 3. Post-processing

- [ ] Post-process chain in the Renderer: ping-pong targets, ordered passes, settings UI (Claude)
- [ ] Bloom (Author)
- [ ] Depth of field (Author)
- [ ] Vignette, chromatic aberration, film grain (Author)
- [ ] Temporal anti-aliasing or FXAA (Author)

## 4. Timeline

- [ ] Timeline panel: play, pause, scrub, frame range, frame rate (Claude)
- [ ] Keyframes on any animatable property, from the Inspector (Claude)
- [ ] Animation curves: linear, bezier, constant; curve editor (Claude)
- [ ] Keyframes and curves in the scene file, undo for every edit (Claude)
- [ ] Time uniform available to every shader (Claude)

## 5. GPU particles

- [ ] Compute pipeline support in the Renderer: storage buffers, dispatch, barriers (Claude)
- [ ] Emitter component: rate, lifetime, shape, start velocity, color (Claude)
- [ ] Force components: gravity, wind, turbulence, attractors (Claude: components and buffers)
- [ ] Emission and simulation compute shaders (Author)
- [ ] Forces and noise fields in the simulation (Author)
- [ ] Particle rendering: billboards, soft particles, blending, sorting (Author)
- [ ] Deterministic playback tied to the timeline, scrubbing, cache (Claude)
- [ ] Particle count and simulation time in the stats overlay (Claude)

## 6. Output

- [ ] Render settings: resolution, frame range, samples (Claude)
- [ ] Offline render loop off the viewport, with progress and cancel (Claude)
- [ ] EXR writer for image sequences, 16 or 32-bit float, linear (Claude)
- [ ] PNG / JPEG output with the display transform applied (Claude)
- [ ] Motion blur by sub-frame accumulation (Author)

## Ideas, not scheduled

- Volumetrics: fog, light shafts (Author)
- Area lights with LTC (Author)
- Screen-space reflections and ambient occlusion (Author)
- Mesh sequences or Alembic import for animated geometry (Claude)
- Node-based material or particle graphs (both)
- USD import / export: scenes, meshes, cameras, lights, UsdPreviewSurface materials (Claude)
- Hydra render delegate, so Lune can render inside Houdini Solaris and other Hydra hosts;
  needs the renderer fed by an adapter instead of reading the editor's `Scene` directly
  (Claude: delegate and adapter, Author: anything the shading needs)

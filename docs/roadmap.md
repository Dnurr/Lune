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

- [x] Node materials: graph model, node editor (Shading workspace), GLSL codegen and runtime compilation, scene library, undo (Claude)
- [x] `main()` in `mesh.frag` takes the surface from `materialSurface()` (Author)
- [x] UVs and tangents in meshes (primitives, OBJ, glTF), submeshes and material slots (Claude)
- [x] Texture loading and caching, sampler slots per material, Image Texture and Texture Coordinate nodes, image drops on the canvas (Claude)
- [x] glTF material import: base color, metallic-roughness, occlusion, normal, emissive as node graphs (Claude)
- [x] `main()` uses `surf.normal`, `surf.occlusion`, `surf.emission` (Author)
- [x] Normal Map node (`node_normal_map`, see docs/materials.md) (Author)
- [ ] Shading nodes in `material_nodes.glsl`: noise, fresnel, triplanar, layering... (Author)
- [x] Alpha modes (opaque, clip, blend) on materials, Material Output `alpha`, glTF alpha import, blended pipelines sorted far to near (Claude)
- [x] Clip materials cut their shadows; translucency map with the light blend materials let through (Claude)
- [x] `main()`: discard below the cutoff, premultiplied output for blend (Author)
- [x] Translucent shadows in `Shadow()` from `shadowTranslucency`, colored (Author)
- [x] Material Output `subsurface`, `subsurfaceColor`, `subsurfaceRadius` (Claude)
- [x] Front depth in the translucency map, for thickness (Claude)
- [x] Subsurface scattering in the light loop: wrap lighting, back light, thickness from the front depth (Author)
- [ ] OBJ materials (.mtl) to node graphs (Claude)

### Viewport shading and image renders

- [x] Shading menu: Wireframe, Clay, Unlit, Preview (environment only), Rendered; environment background toggle (Claude)
- [x] Render Image (F4) and Render Passes (F6) to PNG on their own QRhi, Render Settings, result window (Claude)
- [x] Debug views in `main()` from `lightCount.y`: unlit, normals, depth, direct light only, shadows only (Claude, on the Author's request)
- [x] Render Video (F5): the timeline's frames as PNG files, an MP4 through ffmpeg when installed (Claude)

### Color management

- [ ] Working and display color spaces in the Display menu (Claude)
- [ ] OpenColorIO integration, as an optional dependency (Claude)
- [ ] View transforms as shaders or a 3D LUT in the display pass (Author)

## 3. Post-processing

- [x] Post-process chain in the Renderer: ping-pong targets, ordered passes, settings UI (Claude)
- [x] Scene cameras: Camera component, Camera View with passepartout, renders from the camera (Claude)
- [x] Inspector scrolling: long components (Post Process) in short windows (Claude)
- [x] Bloom: 13-tap downsample with Karis average and threshold, tent upsample (Author)
- [x] Specular anti-aliasing in `mesh.frag` (normal variance added to the GGX roughness) (Author)
- [x] Depth of field: thin-lens circle of confusion, golden-angle gather (Author); half size with a depth-aware composite (Claude)
- [x] Vignette, chromatic aberration, film grain (Author)
- [x] FXAA, the first display effect: edge search along the luma gradient, subpixel blend (Author)
- [ ] Temporal anti-aliasing (Author)

## 4. Timeline

- [x] Timeline panel: play, pause, scrub, frame range, frame rate (Claude)
- [x] Keyframes on any animatable property, from the Inspector; edits of animated properties set keys (Claude)
- [x] Interpolation per key: constant, linear, bezier with auto tangents that don't overshoot (Claude)
- [x] Keyframes in the scene file, undo for every edit, keys of the selection on the timeline (Claude)
- [x] Curve editor: curves of the selection, moving keys in time and value, tangent handles, box select (Claude)
- [x] Panels fold into a strip with their title; Assets and Curves share the bottom like tabs (Claude)
- [x] Time uniform available to every shader: `viewport.w` (meshes), `camera.w` (post) (Claude)

## 5. GPU particles

- [x] Compute pipeline support in the Renderer: storage buffers, dispatch (QRhi orders the passes) (Claude)
- [x] Particle Emitter component: rate, lifetime, shape, velocity, colors, sizes, blend; Add menu, viewport icon (Claude)
- [x] Simulation driver in `particles.comp`: ring buffer, emission spread inside each step, aging, recycling (Claude)
- [x] Deterministic playback tied to the timeline: fixed steps, animated emitters evaluated per step, scrubbing back simulates again (Claude)
- [x] Particle count, emitters and steps in the stats overlay (Claude)
- [x] `emitParticle()` and `updateParticle()` in `particles.comp`: point / sphere / box / disc emission in a spread cone, speed and lifetime variation, gravity, color, opacity and size over life (Author)
- [x] Particle rendering in `particle.vert/.frag`: rotated camera-facing billboards, soft round falloff (Author)
- [x] Force components: gravity, wind, turbulence, attractor, vortex (axis spin, pull, lift, local swirl); on any entity, so they follow it; per-step buffer (Claude)
- [x] `forceAcceleration()` in `particles.comp`: each force type, curl noise for turbulence and the vortex's swirl, reach (Author)
- [x] Particle graphs: a node graph per emitter in the node editor, compiled into the simulation shader at runtime; Scene Forces and Object (follow an entity) built in; values edited without compiling again (Claude)
- [x] `updateParticle()` takes the acceleration from `graphAcceleration()`; force node functions in `particle_nodes.glsl` (Author)
- [ ] More graph outputs: color, size and opacity over life, emission (shape, velocity), kill and collision nodes (Claude: outputs and codegen, Author: nodes)
- [x] Soft particles: the scene's view depth for `particle.frag` (split scene pass, distance pass), per-emitter softness (Claude)
- [x] Soft particles: the fade near surfaces in `particle.frag` (Author)
- [x] Sorting for alpha-blended particles: GPU bitonic sort by view depth, drawn far to near from a sorted copy (Claude)
- [ ] Simulation cache: snapshots every N frames, so long scrubs don't start from the first frame (Claude)

## 6. Volumes and fluid simulation

Smoke, fire, dust and other gas simulations on a 3D grid (Eulerian), rendered by ray marching.

- [ ] Volume container component: a box in the scene with a grid resolution, voxel size, saved settings (Claude)
- [ ] 3D textures for the fields (density, temperature, velocity, pressure, divergence), ping-pong, compute dispatch per field pass (Claude)
- [ ] Simulation on the timeline: fixed steps, re-simulation on edits, a cache of grid snapshots for scrubbing (Claude)
- [ ] Sources: emitters, meshes and particles that add density, temperature and velocity (Claude: sampling and buffers, Author: the injection)
- [ ] Solver passes: advection (semi-Lagrangian, then MacCormack), buoyancy, vorticity confinement, divergence, pressure (Jacobi, later multigrid), projection (Author)
- [ ] Forces of the particle graph applied to the velocity field: wind, turbulence, vortices (both)
- [ ] Volume rendering: ray marching through the box, absorption and scattering, shadows from the lights, emission for fire (Author: shader, Claude: box draw, depth and jitter)
- [ ] Particles advected by the fluid's velocity field (both)
- [ ] VDB export of the fields (Claude, with OpenVDB as an optional dependency)
- [ ] Atmospheric fog and light shafts: froxel grid fitted to the camera, the same scattering code (later)

## 7. Output

- [x] Render settings: resolution, output folder, passes (Claude)
- [x] Render settings: frame range (the timeline's) (Claude)
- [ ] Render settings: samples (Claude)
- [x] Offline render off the viewport (its own QRhi) (Claude)
- [x] Progress and cancel for long renders (Render Video) (Claude)
- [ ] EXR writer for image sequences, 16 or 32-bit float, linear (Claude)
- [x] PNG output with the display transform applied (Claude)
- [ ] JPEG output (Claude)
- [ ] Motion blur by sub-frame accumulation (Author)

## Ideas, not scheduled

- Area lights with LTC (Author)
- Screen-space reflections and ambient occlusion (Author)
- Mesh sequences or Alembic import for animated geometry (Claude)
- USD import / export: scenes, meshes, cameras, lights, UsdPreviewSurface materials (Claude)
- Hydra render delegate, so Lune can render inside Houdini Solaris and other Hydra hosts;
  needs the renderer fed by an adapter instead of reading the editor's `Scene` directly
  (Claude: delegate and adapter, Author: anything the shading needs)

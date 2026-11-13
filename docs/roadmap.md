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

## 2. HDR and shading (current phase)

### Done

- [x] 16-bit float scene target, display pass, exposure (Claude: plumbing, Author: shader)
- [x] Tonemapping: Reinhard, ACES, AgX (Author)
- [x] Undoable display settings (Claude)
- [x] Material component: roughness, metallic (Claude)
- [x] Cook-Torrance BRDF: GGX, Smith, Schlick (Author)
- [x] Light component, directional and point, lights buffer, viewport icons (Claude)
- [~] Light loop in `mesh.frag` (Author). Left to do: add the ambient term once, after
  the loop, not once per light.

### Image-based lighting

- [ ] Load equirectangular HDR images (`.hdr`, later `.exr`) (Claude)
- [ ] World / Environment settings: HDRI, rotation, intensity, saved in the scene (Claude)
- [ ] Cubemap targets, mip chain, one-off precompute passes in the Renderer (Claude)
- [ ] Equirectangular to cubemap conversion shader (Author)
- [ ] Diffuse irradiance convolution (Author)
- [ ] Prefiltered specular cubemap, one roughness level per mip (Author)
- [ ] BRDF integration LUT (Author)
- [ ] Split-sum ambient in `mesh.frag`, replacing the constant ambient (Author)
- [ ] Environment as the viewport background, with a blur option (Author: shader, Claude: toggle)

### Shadows

- [ ] Shadow map render target and light-space depth pass (Claude)
- [ ] Shadow sampling and PCF filtering for the directional light (Author)
- [ ] Point light shadows (cube shadow maps) (Claude: targets, Author: sampling)
- [ ] Cascaded shadow maps for large scenes (Author, later)

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

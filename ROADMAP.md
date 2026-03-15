# Roadmap

## Philosophy

Amber inherits Olive 0.1's greatest strength: everything is where you expect it. Project panel, effects, sequence viewer, timeline with tools on the left, VU meter on the right — no tabs-within-tabs, no empty panels for features you're not using, no clutter. New features must earn their screen space and never compromise that simplicity.

## Support policy

1.x receives bug fixes and security updates throughout 2.0 development — no "rewrite from scratch, old version abandoned". This is an AI-maintained project; as long as capable AI coding agents remain accessible, both branches stay maintained. Once 2.0 is fully released, 1.x reaches end of life.

## 1.x — Stable

The Qt RHI port is complete (1.2.0) — no raw OpenGL remains. Remaining 1.x work:

- **Oak backports** — Cherry-pick QoL features from Olive 0.2 (Oak) that fit the 0.1.x architecture.
- **Code cleanup & refactoring** — Reduce cyclomatic complexity flagged by Lizard, simplify high-CC functions. Exporting amber pipeline as a library? That'll let us add tests.

## 2.0

Major release. Ships progressively via preview releases — features land as they're ready, users always have a working 1.x to fall back on.

### GPU-native effects
Replace common Frei0r CPU plugins with built-in RHI fragment shaders. The existing shader effect infrastructure (XML + GLSL + keyframes) already supports this — it's writing the shaders, not building new architecture. Legacy Frei0r bridge stays available as a compile flag for exotic plugins.

### New built-in effects
- Timer / countdown
- Progress bar
- Lift / gamma / gain (3-way color correction)
- Color correction tool (curves, scopes — waveform, vectorscope, histogram)

### Editing features
- 3-point editing (source in/out marks, insert/overwrite from source monitor)
- Layout presets (Default, 3-Point Editing, Color)

### GPU-only render pipeline
With Frei0r effects replaced by shaders, the CPU image path (`NeedsCpuRgba()`) goes away. Pipeline becomes: FFmpeg decode → YUV upload → GPU effects chain → GPU compositing → readback only for export. CPU fallback is free via QRhi's OpenGL backend + Mesa software rendering.

### .ove → .amb project format
New XML schema for GPU effect parameters. `.ove` import preserved for backward compatibility.

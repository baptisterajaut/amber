# Roadmap

## Philosophy

Amber inherits Olive 0.1's greatest strength: everything is where you expect it. Project panel, effects, sequence viewer, timeline with tools on the left, VU meter on the right — no tabs-within-tabs, no empty panels for features you're not using, no clutter. New features must earn their screen space and never compromise that simplicity.

## Support policy

1.x receives bug fixes and security updates throughout 2.0 development. Once 2.0 is fully released, 1.x reaches end of life.

## 1.x — Maintenance

Core feature work complete. Qt RHI port (1.2.0), audio scrubbing (1.2.4), Oak backports (1.3.0), core/engine layer extraction — all shipped.

### 1.4.0 — Quality-of-life

Self-contained features that don't touch existing architecture. Low risk, high value.

- **3-point editing** — wire footage viewer in/out marks to insert/overwrite actions on timeline. Infrastructure exists (Viewer in/out marks, paste insert mode, focus routing), needs ~200-300 lines of wiring.
- **Subtitle import (.srt)** — new SubtitleEffect with shared style, import from File menu. ~500-600 lines.
- **Duplicate sequence** — `Sequence::copy()` exists, needs a menu item. ~10 lines.
- **Export single frame** — `RenderThread::save_fn` callback exists, needs a File menu action. ~20 lines.
- **Match frame** — clip under playhead → open source in footage viewer at matching frame. ~50 lines.
- **Freeze frame** — split at playhead + hold frame. ~80 lines.
- **Built-in export presets** — ship preset XML files (YouTube 1080p, ProRes, etc.). 0 code changes.
- **Welcome screen** — QDialog at startup with recent projects + new project with sequence templates. ~200-300 lines.
- **Nesting fixes** — gizmos don't display on nested sequence clips (transform values work, overlay doesn't — likely `GetSelectedGizmo()` skips `MEDIA_TYPE_SEQUENCE` clips). Circular reference detection in `compose_sequence()` (cycle in `params.nests` = skip, ~2 lines runtime + ~30 lines drop guard in `TimelineWidget::dropEvent()`), deletion protection for referenced sequences (~50 lines), sequence navigation stack in OliveGlobal + Escape to go back (~50 lines), breadcrumb in timeline header (~50 lines).
- **Flatten/unpack nested sequence** — inverse of nest(), restore inner clips to parent timeline. ~150 lines.
- **Color labels menu consistency** — context menu still shows color label options when color labels are disabled in View. Hide or disable recolor options when `show_color_labels` is off.
- **Audio data race fix** — lock `audio_write_lock` around AudioSenderThread buffer reads (correctness bug).

### 1.x maintenance

Bug fixes and security patches only.

## 2.0

Major release. Ships progressively via preview releases — features land as they're ready, users always have a working 1.x to fall back on.

### GPU-native effects

Replace common per-pixel Frei0r plugins (brightness, contrast, blur, chroma key, distortions…) with built-in RHI fragment shaders. The existing shader effect infrastructure (XML + GLSL + keyframes) already supports this — it's writing the shaders, not building new architecture. ~15-20 new shaders needed for remaining common Frei0r equivalents (levels, curves, sharpen, basic denoise). Each shader: 20-80 lines GLSL + XML.

Internal C++ effects (Transform, Text, Solid, Timecode) stay as-is — they use SuperimposeFlag, not ImageFlag. Temporal Frei0r effects (deshake, motion stabilization) have no GPU equivalent — they require multi-frame analysis and optical flow, which are inherently CPU algorithms. These stay as Frei0r and keep the `NeedsCpuRgba()` path. A `-DFREI0R_LEGACY` compile flag keeps the full bridge for users who depend on niche Frei0r plugins.

**Risks:** Visual regression vs Frei0r originals (A/B comparison testing needed). Some effects may need multi-pass (3-way color corrector, denoise).

### ShaderToy effect import

Amber 2.0 accepts fragment shaders written in ShaderToy format — the de facto community standard for GPU effects, with thousands of filters, generators, and transitions available at shadertoy.com.

**How it works:** ShaderToy shaders use a fixed entry point (`void mainImage(out vec4 fragColor, in vec2 fragCoord)`) with standard uniforms. Amber preprocesses them at load time into GLSL 440 compatible with QShaderBaker: injects the UBO declaration, sampler bindings, and a `main()` wrapper. The compiled `.qsb` is disk-cached — recompilation only on source change.

**Standard uniforms mapped automatically:**
- `iChannel0` → clip input texture
- `iResolution` → sequence dimensions (`vec3(w, h, 1.0)`)
- `iTime` → clip timecode in seconds
- `iFrame` → frame index
- `iTimeDelta` → `1 / frame_rate`
- `iDate`, `iMouse` → provided, usable

**Backend portability:** ShaderToy shaders receive `fragCoord` as `vTexCoord × iResolution.xy` in the generated wrapper — not raw `gl_FragCoord` — which avoids the Y-axis flip difference between Vulkan/Metal/D3D (top-left origin) and OpenGL (bottom-left origin).

**To use:** Drop a `.shadertoy` file into the effects folder — it appears in the effect list under the *ShaderToy* category. Or paste directly via *Effects → Import ShaderToy shader*. Compilation errors surface as a warning indicator on the effect strip with the baker error message.

**Custom parameters:** By default a ShaderToy effect has no user-facing sliders — standard uniforms are automatic. To expose parameters, add a minimal XML sidecar declaring `<field>` entries; those become slider rows in the Effect Controls panel, same as built-in shader effects.

**v1 scope (2.0):** Single-pass, single-input (`iChannel0`) shaders. Out of scope for 2.0:
- Multi-pass shaders (ShaderToy Buffer A/B/C/D) — requires persistent textures across frames
- `iChannel1–3` media assignment — requires UI to assign sources and extra sampler bindings
- Audio FFT input (`iChannel` of type audio)

### New built-in effects
- Timer / countdown
- Progress bar
- Lift / gamma / gain (3-way color correction)
- Color correction tool (curves, scopes — waveform, vectorscope, histogram)
- Subtitle effect with .srt import (SubtitleEffect — shared style, dedicated floating editor for bulk operations, fine-tuning via Effect Controls)

### Scopes & monitoring

Backported from Oak, adapted to QRhi (originally GL-based):
- Waveform + histogram scopes
- Pixel sampler
- Enhanced audio monitor
- FPS counter overlay

### Editing features
- Track mute/solo/lock (per-track header controls + skip logic in compose_sequence)
- Layout presets (Default, 3-Point Editing, Color)
- Color labels on media in project panel (field exists on Clip, needs UI on Media)
- Proxy toggle (switch proxy/full-res during playback without rebuild)

### Rendering pipeline optimizations

**GPU-only pipeline:** With per-pixel Frei0r effects replaced by shaders, the CPU image path (`NeedsCpuRgba()`) is limited to clips carrying temporal Frei0r effects (deshake, stabilization). Pipeline for all other clips: FFmpeg decode → YUV upload → GPU effects chain → GPU compositing → readback only for export. CPU fallback is free via QRhi's OpenGL backend + Mesa software rendering.

**RHI resource caching:** Currently ~16 QRhi resource allocs+destructs per clip per frame (buffers, SRBs, pipelines). Pipeline creation is 100µs+ on Vulkan. Pre-create and cache pipelines/SRBs per clip, use a buffer pool for dynamic buffers.

**Pre-allocated YUV staging buffers:** Clip::Retrieve() currently constructs new QByteArrays per frame for YUV plane upload — 3.0 MB of heap alloc per 1080p frame. Pre-allocate persistent buffers to eliminate churn. (Note: zero-copy is not feasible — Qt RHI's `QRhiTextureSubresourceUploadDescription` only accepts QByteArray, no raw pointer API. The copy overhead itself is negligible at 0.45% CPU bandwidth.)

**Lower-res readback during scrub:** Full GPU→CPU→GPU round-trip on every frame (8.3 MB for 1080p). Half-res readback during scrub would cut bandwidth 4x.

**Viewer overlays via QRhi:** The viewer overlay (title-safe, guides, gizmos) currently uses a sibling QWidget with QPainter. Render directly in the QRhi pipeline — eliminates the extra compositing layer and fixes the QWidget-over-QRhiWidget issue on Vulkan backend.

### Audio pipeline cleanup

The audio pipeline's shared control variables (`audio_ibuffer_frame`, `audio_reset_`, `audio_ibuffer_read`) have been upgraded to `std::atomic<>`. However, `audio_ibuffer` (the 192KB ring buffer itself) is read by AudioSenderThread without `audio_write_lock`, creating a data race with Cacher writes. Fix: lock around `send_audio_to_output()` buffer reads.

**Scrub performance:** Pre-allocate the staging buffer in `AudioSenderThread` (currently heap-allocates on every scrub frame). Skip audio grain entirely on fast scrub (<16ms between seeks) — only produce audio when scrub settles, like Premiere/Resolve.

### Export improvements
- Hardware encoding (NVENC, VAAPI, QSV) — hwaccel decoding exists, expose FFmpeg encoder variants in export dialog
- Render queue — non-modal export with queue UI, continue editing during render
- Batch export — multi-sequence export in one operation

### .ove → .amb project format

New XML schema for GPU effect parameters. `.ove` import preserved for backward compatibility.

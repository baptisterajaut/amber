# Roadmap

## Philosophy

Amber inherits Olive 0.1's greatest strength: everything is where you expect it. Project panel, effects, sequence viewer, timeline with tools on the left, VU meter on the right — no tabs-within-tabs, no empty panels for features you're not using, no clutter. New features must earn their screen space and never compromise that simplicity.

## Support policy

1.x receives bug fixes and security updates throughout 2.0 development. Once 2.0 is fully released, 1.x reaches end of life.


### 1.6 — Shipped

- ~~Auto Cut Silence ripple delete + configurable gap between clips, feedback dialogs when no cuts produced~~ (shipped in v1.5.3)
- ~~Track Select Tool — selects all clips from click point rightward on the track (Shift = all tracks)~~ (shipped in v1.6.0, #15)
- ~~Shift+Arrow multi-frame skip — alias for Jump Backward/Forward, configurable step in preferences~~ (shipped in v1.6.0, #15)
- ~~Bold timecodes on viewer~~ (shipped in v1.6.0, #12)

### 1.7+ — Quality-of-life (pending)

- **Canvas Painter for viewer overlays (Qt 6.11)** — replace QPainter with Qt Canvas Painter (GPU-accelerated 2D on QRhi, 2-10x faster) for title-safe, guides, gizmos. Drop-in API swap, same drawing logic. Tech preview — API may shift in 6.12.
- **Callback-based audio I/O (Qt 6.11)** — `QAudioSink::start()` now accepts a callback for real-time audio processing, replacing QIODevice push/pull. Adopt for audio monitoring and scrub playback — cleaner, lower latency.
- **PipeWire Bluetooth audio** — Qt 6.11 ships a new PipeWire backend. Test if Bluetooth sink enumeration works; if so, remove the `QT_AUDIO_BACKEND=pulseaudio` workaround in `main.cpp`.
- **Flatpak / Flathub release** — provide an official Flatpak manifest and publish to Flathub for tighter desktop integration on Linux (file portals, permissions). Complements the existing AppImage. Requires a Flathub account + ongoing manifest maintenance — likely a community contribution. (#41)

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
- Gradient generator — linear/radial with multi-stop color ramp, applicable to solids and text (#21)
- Timer / countdown
- Progress bar
- Lift / gamma / gain (3-way color correction)
- Color correction tool (curves, scopes — waveform, vectorscope, histogram)
- Subtitle editor — dedicated floating window for bulk subtitle editing (import is shipped in 1.5.0, this is the full editing UI)
- **Text stroke** — QPainterPath outline on rich text effect (#12)
- **Built-in audio effects** — EQ (parametric), compressor, reverb, delay, chorus, limiter. Incremental — each effect is independent DSP. (#12)
- **Turbulent displacement** — noise-driven UV warp for stop-motion / glitch / heat-haze looks. Single-pass fragment shader (Perlin/simplex noise → displacement vector → texture lookup), fits the existing XML+GLSL shader effect infrastructure. Also reachable via ShaderToy import once that lands. (#35)
- **Motion blur** — shutter-angle integration on animated transforms: at render time, sample the Transform effect's keyframes at N sub-frame positions within the shutter window and accumulate. Per-clip toggle + global default. Scope: animation-driven motion blur only — true per-pixel motion vectors (camera/object motion in source footage) is post-2.0, requires optical flow. (#36)

### Scopes & monitoring

Backported from Oak, adapted to QRhi (originally GL-based):
- Waveform + histogram scopes
- Pixel sampler
- Enhanced audio monitor
- FPS counter overlay

### Editing features
- Track mute/solo/lock with track headers (per-track header controls showing V1/A1 labels, show/hide for video, mute/solo for audio + skip logic in compose_sequence) (#12)
- Linked clip vertical drag — V+A clips move together across tracks when dragging a linked pair, standard NLE behavior (#12)
- Layout presets (Default, 3-Point Editing, Color)
- Color labels on media in project panel (field exists on Clip, needs UI on Media)
- Proxy toggle (switch proxy/full-res during playback without rebuild)
- Markers with duration — range markers with in/out points, Premiere-style section marking (#15)
- Effect presets save/load — serialize effect parameters, apply saved presets to clips (#15)
- **Adjustment Layer** — non-destructive overlay clip on a video track; effects on it apply to all tracks visually beneath within its time range. Unlike nested sequences, it doesn't reorganize the timeline. Implementation: new medialess clip kind, branch in `compose_sequence()` feeding the running framebuffer as input texture (ping-pong buffer to avoid read/write hazard), "New > Adjustment Layer" UI, `.ove` serialization. ~2-4 days. (#32)
- **Voice-over recording UX polish** — recording itself already works: dedicated record button on the timeline toolbar arms the Add Audio mode, then click on the timeline to choose a track / drag a region, Play starts recording, Stop saves a WAV next to the project and inserts a clip. Input device + mono/stereo configurable in Preferences. What's missing: a live input VU meter while armed/recording, optional pre-roll countdown. (#39)
- **Audio waveform sync** — auto-align two or more audio clips by cross-correlation (FFT-based) to sync external mic with camera audio across multi-cam takes. Manual sync-by-marker as a fallback. Locks aligned clips together as a linked group. (#40)

### Rendering pipeline optimizations

**GPU-only pipeline:** With per-pixel Frei0r effects replaced by shaders, the CPU image path (`NeedsCpuRgba()`) is limited to clips carrying temporal Frei0r effects (deshake, stabilization). Pipeline for all other clips: FFmpeg decode → YUV upload → GPU effects chain → GPU compositing → readback only for export. CPU fallback is free via QRhi's OpenGL backend + Mesa software rendering.

**RHI resource caching:** Currently ~16 QRhi resource allocs+destructs per clip per frame (buffers, SRBs, pipelines). Pipeline creation is 100µs+ on Vulkan. Pre-create and cache pipelines/SRBs per clip, use a buffer pool for dynamic buffers.

**Pre-allocated YUV staging buffers:** Clip::Retrieve() currently constructs new QByteArrays per frame for YUV plane upload — 3.0 MB of heap alloc per 1080p frame. Pre-allocate persistent buffers to eliminate churn. (Note: zero-copy is not feasible — Qt RHI's `QRhiTextureSubresourceUploadDescription` only accepts QByteArray, no raw pointer API. The copy overhead itself is negligible at 0.45% CPU bandwidth.)

**Lower-res readback during scrub:** Full GPU→CPU→GPU round-trip on every frame (8.3 MB for 1080p). Half-res readback during scrub would cut bandwidth 4x.

**Viewer overlays via QRhi:** The viewer overlay (title-safe, guides, gizmos) currently uses a sibling QWidget with QPainter. 1.6.0 swaps QPainter for Canvas Painter (Qt 6.11); 2.0 goes further by rendering overlays directly in the QRhi compositing pass — eliminates the extra widget layer and fixes QWidget-over-QRhiWidget on Vulkan backend.

### FFmpeg API migration

- **Buffersink `channel_layouts` → `ch_layouts`** — FFmpeg 7+ deprecates the `channel_layouts` option on abuffersink in favor of `ch_layouts` (string). However, `ch_layouts` with "stereo" silently fails the filter init on some versions (returns success, configures nothing). Current code uses `channel_layouts` first with `ch_layouts` fallback — works on all versions (3.4–8) but prints a cosmetic deprecation warning on FFmpeg 7+. Revisit when FFmpeg removes `channel_layouts` or when the `ch_layouts` string parsing is fixed upstream.

### Audio pipeline cleanup

The audio data race (`audio_ibuffer` read without lock) was fixed in 1.4.0. Remaining work:

**Scrub performance:** Pre-allocate the staging buffer in `AudioSenderThread` (currently heap-allocates on every scrub frame). Skip audio grain entirely on fast scrub (<16ms between seeks) — only produce audio when scrub settles, like Premiere/Resolve. 1.6.0 adopts Qt 6.11 callback-based `QAudioSink::start()` (drop-in); 2.0 redesigns the audio threading model around it.

### Export improvements
- Hardware encoding (NVENC, VAAPI, QSV) — hwaccel decoding exists, expose FFmpeg encoder variants in export dialog
- Render queue — non-modal export with queue UI, continue editing during render
- Batch export — multi-sequence export in one operation

### UI polish
- **Welcome screen** — QDialog at startup with recent projects + new project with sequence templates.
- **Effect controls alignment** — align labels and values in a grid layout (labels left-aligned, values right-aligned with consistent weight). Touches `CollapsibleWidget` + `EffectRow` layout. (#12)
- **Audio plugin parameters in EffectControls** — expose VST2 parameters as native EffectField rows instead of "open GUI" button only. Depends on plugin API exposing param metadata. (#12)
- **Graph Editor improvements** — better curve editing UX, currently minimal on 0.1.x (#15)
- **Compact timeline mode for small screens** — finer minimum on audio track height, optional collapsed-track display, denser timeline ruler. Useful on low-vertical-resolution laptops where the timeline currently eats too much screen space. (#38)

### .ove → .amb project format

New XML schema for GPU effect parameters. `.ove` import preserved for backward compatibility.

## Future (post-2.0)

Features that require major architectural work or are outside the current scope.

- **LADSPA audio plugin support** — lightweight C API, dlopen-based. Most realistic audio plugin standard to add beyond VST2. VST3/LV2/CLAP deferred — hosting SDKs are heavy and conflict with the lightweight footprint goal. (#12)
- **2.5D compositing** — per-layer Z-depth with perspective camera. Requires 3D projection matrix in `compose_sequence()`, Z-order per clip, camera node. Major architectural change. (#12)
- **Text animation** — letter-by-letter, word-by-word, line-by-line transforms + typewriter effect. Requires a mini animation engine within the text effect. (#12)
- **2.5D motion tracker** — point/planar tracking with compositing integration. Requires optical flow or feature matching (CPU-bound). (#12)

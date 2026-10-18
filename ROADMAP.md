# Roadmap

## Philosophy

Amber inherits Olive 0.1's greatest strength: everything is where you expect it. Project panel, effects, sequence viewer, timeline with tools on the left, VU meter on the right — no tabs-within-tabs, no empty panels for features you're not using, no clutter. New features must earn their screen space and never compromise that simplicity.

## Support policy

1.x receives bug fixes and security updates throughout 2.0 development. Once 2.0 is fully released, 1.x reaches end of life.


### 1.6.0 — Quality-of-life

- ~~Auto Cut Silence ripple delete + configurable gap between clips, feedback dialogs when no cuts produced~~
- ~~Track Select Tool — new timeline tool, selects all clips from click point rightward on the track (Shift = all tracks) (#15)~~ — shipped in v1.6.0
- ~~Shift+Arrow multi-frame skip — bind Shift+Left/Right as alias for Jump Backward/Forward (existing `Ctrl+[`/`Ctrl+]`, configurable step in preferences) (#15)~~ — shipped in v1.6.0
- **Canvas Painter for viewer overlays (Qt 6.11)** — replace QPainter with Qt Canvas Painter (GPU-accelerated 2D on QRhi, 2-10x faster) for title-safe, guides, gizmos. Drop-in API swap, same drawing logic. Tech preview — API may shift in 6.12.
- **Callback-based audio I/O (Qt 6.11)** — `QAudioSink::start()` now accepts a callback for real-time audio processing, replacing QIODevice push/pull. Adopt for audio monitoring and scrub playback — cleaner, lower latency.
- ~~**Bold timecodes** — increase font weight on timecode displays (viewer, effect controls). Trivial QSS/font change. (#12)~~ — shipped in v1.6.0
- **PipeWire Bluetooth audio** — Qt 6.11 ships a new PipeWire backend. Test if Bluetooth sink enumeration works; if so, remove the `QT_AUDIO_BACKEND=pulseaudio` workaround in `main.cpp`.



## 2.0

Major release. Ships progressively via preview releases — features land as they're ready, users always have a working 1.x to fall back on.

### GPU-native effects

Replace common per-pixel Frei0r plugins (brightness, contrast, blur, chroma key, distortions…) with built-in RHI fragment shaders. The existing shader effect infrastructure (XML + GLSL + keyframes) already supports this — it's writing the shaders, not building new architecture. ~15-20 new shaders needed for remaining common Frei0r equivalents (levels, curves, sharpen, basic denoise). Each shader: 20-80 lines GLSL + XML.

Done in 2.0 so far: Levels (input/output black and white, gamma, per channel), Lift/Gamma/Gain (per-channel, the shader the color correction tool will drive), Tint (shadows/highlights colors, sepia by default), Threshold, Lens Correction (K1/K2 radial, center, zoom), Find Edges (Sobel, luma or per channel; replaces Olive's broken disabled one), Colorize (hue/saturation, keeps luma), Alpha Adjust (invert, threshold, show alpha), Glow (threshold, radius, intensity, Add/Screen; multipass shaders can now read the effect's original input in every pass), Denoise (spatial, one-pass bilateral: radius, strength, detail), Curves (RGB / Luma / R / G / B tone curves with an in-panel curve editor, see Tone Curve below). Every common Frei0r equivalent is covered now; temporal denoise (hqdn3d) stays Frei0r.

Internal C++ effects (Transform, Text, Solid, Timecode) stay as-is — they use SuperimposeFlag, not ImageFlag. Temporal Frei0r effects (deshake, motion stabilization) have no GPU equivalent — they require multi-frame analysis and optical flow, which are inherently CPU algorithms. These stay as Frei0r and keep the `NeedsCpuRgba()` path. Frei0r stays built in but hidden: its effects are left out of the add-effect menus unless Preferences > Behavior > "Show Frei0r effects (CPU, slower)" is on (off by default). Opening a project that uses Frei0r effects shows them for as long as that project is open, without touching the preference; a new project, or one without Frei0r effects, hides them again. Hidden effects still load, render, copy and paste. A `-DNOFREI0R` build keeps a project's Frei0r effects as "Missing Effect" placeholders that save back unchanged, and says once per load that Frei0r plugins were not compiled into this version.

**Risks:** Visual regression vs Frei0r originals (A/B comparison testing needed). Some effects may need multi-pass (3-way color corrector, denoise).

### ShaderToy effect import

Amber 2.0 accepts fragment shaders written in ShaderToy format — the de facto community standard for GPU effects, with thousands of filters, generators, and transitions available at shadertoy.com.

**Done in 2.0 (v1 scope):** a `.shadertoy` file (the shader's "Image" tab, as is) in an effects folder appears under the *ShaderToy* category of the add-effect menu, or *Add Video Effect > Import ShaderToy Shader...* takes a name and a pasted source, compiles it (errors keep the dialog open) and writes it to the user effects folder. A failed compile shows a warning icon on the effect strip with the baker message, and the effect renders as a no-op.

**How it works:** ShaderToy shaders use a fixed entry point (`void mainImage(out vec4 fragColor, in vec2 fragCoord)`) with standard uniforms. At bake time Amber turns the file into the GLSL 440 an XML shader effect would ship (`core/shadertoy`): injects the UBO declaration, the `iChannel0` binding and a `main()` wrapper, blanks `#version`/`precision`/the copied input declarations, and routes the texture reads through flipping helpers. The compiled `.qsb` is disk-cached — recompilation only on source change.

**Standard uniforms mapped automatically:**
- `iChannel0` → clip input texture (unpremultiplied for the shader)
- `iResolution`, `iChannelResolution[0]` → the clip texture size (`vec3(w, h, 1.0)`; reduced during half-res scrub), so `fragCoord / iResolution.xy` is the `iChannel0` uv
- `iTime` → clip timecode in seconds (clip-local: trimming the head continues the animation)
- `iFrame` → frame index, `iTimeDelta` → `1 / frame_rate`
- `iDate` → wall clock (an export reading it isn't reproducible), `iMouse` → 0

**Backend portability:** the wrapper derives `fragCoord` from the texture coordinate (y flipped to ShaderToy's y-up) — not raw `gl_FragCoord` — and the `iChannel0` reads are flipped the same way, so a passthrough is an exact identity on every backend.

**Alpha:** the shader's alpha is ignored; the output keeps the clip's alpha (`rgb × a, a`), so nothing leaks onto the tracks below. A ShaderToy shader can't create or move alpha in v1.

**Custom parameters:** By default a ShaderToy effect has no user-facing sliders — standard uniforms are automatic. To expose parameters, add a sibling `<name>.xml` sidecar (a normal effect XML whose `<shader frag="<name>.shadertoy"/>`) declaring `<field>` entries; those become uniforms and slider rows in the Effect Controls panel, same as built-in shader effects.

**v1 scope (2.0):** Single-pass, single-input (`iChannel0`) shaders. Projects reference the effect by name, the `.shadertoy` must travel with a shared `.ove`. Out of scope for 2.0:
- Multi-pass shaders (ShaderToy Buffer A/B/C/D) — requires persistent textures across frames
- `iChannel1–3` media assignment — requires UI to assign sources and extra sampler bindings
- Audio FFT input (`iChannel` of type audio)
- `textureGrad`/`textureGather`/`texture*Offset`/`textureProj` on `iChannel0` (rejected with a message: the flip can't be applied)

### New built-in effects
- Gradient generator — 2-stop linear/radial shipped in v1.7.0; 2.0 adds "As Mask" to fill the Text / Rich Text letters above it. Remaining: a multi-stop color ramp (#21)
- Timer / countdown — done in 2.0: Timer generator, counts up from the clip's start or down to 0 on its last frame, MM:SS / HH:MM:SS / seconds / hundredths, prefix and time offset
- Progress bar — done in 2.0: Progress Bar generator, fills over the clip's duration or follows a keyframeable value, top or bottom edge, thickness, margin, direction
- Drop Shadow — done in 2.0: drawn in sequence space from the clip as already transformed, so it works on opaque picture-in-picture clips; colour, opacity, direction, distance, softness, all keyframeable
- Lift / gamma / gain — done in 2.0: Lift/Gamma/Gain shader effect (Color), one of the ten GPU effects replacing common Frei0r plugins
- **Tone Curve** — done in 2.0: Curves effect (Color), a square curve editor in Effect Controls with RGB / Luma / R / G / B curves (monotone cubic through the points), applied through a 256-entry lookup texture. The curve field is generic (`<field type="curve">`), usable by any XML shader effect. Not keyframeable yet. Complementary to the 3-way corrector. (#59)
- **Alpha Mask (image source)** — done in 2.0: Alpha Mask effect (Keying), takes a PNG / JPEG / WebP / BMP / SVG file and keeps the clip where the mask is opaque, or white with the Luminance channel. Stretch / Fit / Fill, position, scale, feather, invert, opacity, keyframeable except the file and the combos. Pinned to the clip (applied before Transform, moves with it). No blend controls: a stencil only multiplies alpha, blending belongs to track compositing. Covers text reveals, shape stencils, static masks. Track-based variant (mask = clip on adjacent track) deferred — see Future. (#61)
- Color correction tool (scopes — waveform, vectorscope, histogram) — done in 2.0: see the Scopes panel under Scopes & monitoring
- Subtitle editor — dedicated floating window for bulk subtitle editing (import is shipped in 1.5.0, this is the full editing UI)
- ~~Text stroke — outline color + width on the Rich Text effect~~ (shipped in v1.7.0, #12)
- **Built-in audio effects** — EQ (parametric), compressor, reverb, delay, chorus, limiter. Incremental — each effect is independent DSP. (#12)
- **LUT loader** — done: LUT effect (Color) loads a `.cube` 3D LUT with a Strength blend (#77), on 2.0.x and on main for the next 1.7 release

### Scopes & monitoring

Backported from Oak, adapted to QRhi (originally GL-based):
- Waveform + histogram scopes — done in 2.0: Scopes panel (Window > Scopes, hidden by default) with luma waveform, RGB parade, histogram and vectorscope of the sequence viewer's frame, computed on the CPU from the viewer's readback (`core/scopes`, `panels/scopespanel`, `ui/scopewidget`)
- Pixel sampler — done in 2.0: View > Show Pixel Sampler (off by default) shows, in the bottom-left corner of either viewer, a swatch and the pixel under the pointer (`X 812  Y 403   R 210  G 180  B 96  A 255`): position in sequence pixels even at a reduced preview resolution, 8-bit straight-alpha values (the premultiplied front buffer divided by alpha), as an export with alpha writes them; the eyedropper picks the same straight colour. Follows the mouse and each new frame under a still pointer, hides off the frame, outside the viewer and while the eyedropper is active. The eyedropper now shares its mapping, so both are right at a zoom past the viewer's size (`core/pixelsampler`, `amber-test-pixel-sampler`)
- Enhanced audio monitor
- FPS counter overlay — done in 2.0: View > Show Frame Rate (off by default) labels the sequence viewer's top-left corner during playback with the displayed rate over the last second and the sequence rate (`29.7 / 29.97 fps`), red when a full second averages below 95% of the sequence rate. Counts the frames the viewer painted that show a new sequence frame, so frames dropped between two paints and playback-timer re-renders of the same frame don't count (`core/fpsmeter`, `amber-test-fps-meter`)

### Editing features
- Track mute/solo/lock with track headers (per-track header controls showing V1/A1 labels, show/hide for video, mute/solo for audio + skip logic in compose_sequence) (#12)
- **Per-track volume and pan** — simple mixer controls in the track headers, next to mute/solo. Applied in the audio mix, not as a clip effect.
- Linked clip vertical drag — done in 2.0: dragging a clip up or down a track moves the selected clips on the other side (video or audio) the mirrored way, away from or toward the V/A seam, so a linked V1+A1 pair dragged to V2 lands on V2+A2. A ghost that would cross the seam blocks the vertical move of all of them (the drag still moves in time), so their layout is kept. Track arithmetic in `panels/timeline_layout` (`ghost_track`, `clamp_track_diff`), tested in `amber-test-timeline-layout` (#12)
- Layout presets (Default, 3-Point Editing, Color)
- ~~Color labels on media in project panel~~ (shipped in v1.7.0)
- Proxy toggle — done in 2.0: Use Proxies (on by default), in the viewer's preview resolution menu and in View (keymap id `useproxies`, no default shortcut). Off, every clip whose footage has a proxy switches to the original file, live, during playback too, without regenerating anything; on, back to the proxy. Export always decodes the original; Save Frame follows the viewer. `Clip::Open()` latches the setting next to the export mode, and `NeedsCacherReconfigure()` reopens a proxied clip whose latch no longer matches (`amber-engine-tests`, `viewerFollowsUseProxies` in `amber-test-headless-render`)
- Markers with duration — done in 2.0: a marker can span a section, drawn as a band in the rulers and on clips. Drag its end tick to resize it, Alt-drag a point marker to give it a duration, or set Duration in Marker Properties. Range ends are snap points. Saved as a `duration` attribute that 1.x ignores. (#15)
- Effect presets — done in 2.0: right-click an effect's title, Save as Preset... keeps the selected effects (values, keyframes) under a name; the Presets submenu at the top of Add Video / Add Audio Effect applies one to the selected clips in one undo step, or deletes it. Stored in the user data folder, one XML file per preset. No effects browser panel yet. (#15)

### Rendering pipeline optimizations

**GPU-only pipeline:** With per-pixel Frei0r effects replaced by shaders, the CPU image path (`NeedsCpuRgba()`) is limited to clips carrying temporal Frei0r effects (deshake, stabilization). Pipeline for all other clips: FFmpeg decode → YUV upload → GPU effects chain → GPU compositing → readback only for export. CPU fallback is free via QRhi's OpenGL backend + Mesa software rendering.

**RHI resource caching** — done in 2.0: graphics pipelines are cached per QRhi (`RhiResourceCache`, keyed by shaders, blend, binding shape and render pass format), per-pass dynamic buffers come from a per-frame pool, so steady-state compositing creates zero pipelines and zero buffers per frame (pinned by `steadyStateCreatesNothing` in `amber-test-effects-gpu`). SRBs are still created per pass (cache them behind a resource-deletion hook if they show in a profile).

**Measuring:** `amber-bench-render` (not a ctest) gives median/p95 ms per 1080p frame on fixed scenarios, `--rhi gl|vulkan`. Run it before and after each change above with `--frames 300` or more (median noise is about ±0.1 ms). Baseline on an RX 6900 XT, 100 frames: GL solid 1.0 / fx5 2.4 / stack4 1.8 ms, Vulkan 0.85 / 1.28 / 1.27 ms. After RHI resource caching, 300 frames, median of 3 runs, same machine (hardware GPU: radeonsi for GL on offscreen QPA, RADV for Vulkan under xvfb-run), before → after: GL solid 1.06 → 0.87 ms (−18%), fx5 2.51 → 1.65 ms (−34%, p95 2.9 → 1.9), stack4 1.84 → 1.26 ms (−31%); Vulkan solid 0.88 → 0.90 ms (flat), fx5 1.38 → 1.28 ms (−7%), stack4 1.33 → 1.27 ms (−5%).

**Pre-allocated YUV staging buffers:** Clip::Retrieve() currently constructs new QByteArrays per frame for YUV plane upload — 3.0 MB of heap alloc per 1080p frame. Pre-allocate persistent buffers to eliminate churn. (Note: zero-copy is not feasible — Qt RHI's `QRhiTextureSubresourceUploadDescription` only accepts QByteArray, no raw pointer API. The copy overhead itself is negligible at 0.45% CPU bandwidth.)

**Half resolution while scrubbing** — done in 2.0: Preferences > Playback > Scrubbing, Auto (default) / Always / Never. Scrub frames (every seek-driven render: ruler or playhead drag, clicks, jumps; prev/next frame stay full res) render at twice the preview divider (capped at 8, short side kept ≥ 64 px), so the GPU→CPU readback carries a quarter of the pixels. Auto switches a gesture to half res after 2 consecutive full-res scrub frames whose GPU + readback time exceeds one frame budget (`1000 / frame rate` ms); that time excludes decoder waits, so a decode-bound scrub (long-GOP seeks) never triggers it, and it doesn't fire at 1080p on a fast GPU. 150 ms after the last seek the frame is re-rendered precisely at the normal preview resolution; releasing the ruler re-renders it at once (the ruler's release refresh is a normal render). Logic in `core/scrubresolution` (`amber-test-scrub-resolution`).

**Viewer overlays via QRhi:** The viewer overlay (title-safe, guides, gizmos) currently uses a sibling QWidget with QPainter. 1.6.0 swaps QPainter for Canvas Painter (Qt 6.11); 2.0 goes further by rendering overlays directly in the QRhi compositing pass — eliminates the extra widget layer and fixes QWidget-over-QRhiWidget on Vulkan backend.

### FFmpeg API migration

- **Buffersink `channel_layouts` → `ch_layouts`** — FFmpeg 7+ deprecates the `channel_layouts` option on abuffersink in favor of `ch_layouts` (string). However, `ch_layouts` with "stereo" silently fails the filter init on some versions (returns success, configures nothing). Current code uses `channel_layouts` first with `ch_layouts` fallback — works on all versions (3.4–8) but prints a cosmetic deprecation warning on FFmpeg 7+. Revisit when FFmpeg removes `channel_layouts` or when the `ch_layouts` string parsing is fixed upstream.

### Audio pipeline cleanup

The audio data race (`audio_ibuffer` read without lock) was fixed in 1.4.0. Remaining work:

**Scrub performance:** Pre-allocate the staging buffer in `AudioSenderThread` (currently heap-allocates on every scrub frame). Skip audio grain entirely on fast scrub (<16ms between seeks) — only produce audio when scrub settles, like Premiere/Resolve. 1.6.0 adopts Qt 6.11 callback-based `QAudioSink::start()` (drop-in); 2.0 redesigns the audio threading model around it.

### Export improvements
- Hardware encoding (NVENC, VAAPI, QSV) — hwaccel decoding exists, expose FFmpeg encoder variants in export dialog
- **Per-codec options panel** — generalize the export dialog so each codec declares its own parameter set instead of one-off field additions. Concrete asks driving the redesign (#58): x264/x265 `-preset` (veryslow→ultrafast), SVT-AV1 (preset −2..13, CRF 0..63), rav1e (`-speed` −1..10, `-qp` 0..255), libaom-av1, AV1 2-pass encoding, average-bitrate as a compression mode for x264/x265/SVT-AV1/rav1e, FLAC compression levels, `+faststart` movflag for MOV-family containers (MP4/MOV/3GPP/FLV), per-codec pixel format filtering.
- Render queue — non-modal export with queue UI, continue editing during render
- Batch export — multi-sequence export in one operation
- **Master loudness on export** — optional loudness normalization (EBU R128 `loudnorm`, e.g. −14 LUFS for YouTube) and brickwall limiter (`alimiter`) on the final mix. The export audio path uses libswresample only today, so this adds a small libavfilter graph before the encoder.

### UI polish
- **Welcome screen** — QDialog at startup with recent projects + new project with sequence templates.
- **Effect controls alignment** — done in 2.0: every effect's values start at one shared column, after the widest label of the expanded effects, and wide fields end at the same edge. Values stay left-aligned at that column rather than right-aligned. (#12)
- **Audio plugin parameters in EffectControls** — expose VST2 parameters as native EffectField rows instead of "open GUI" button only. Depends on plugin API exposing param metadata. (#12)
- **Graph Editor improvements** — better curve editing UX, currently minimal on 0.1.x (#15)

### .ove → .amb project format

New XML schema for GPU effect parameters. `.ove` import preserved for backward compatibility.

## Future (post-2.0)

Features that require major architectural work or are outside the current scope.

- **LADSPA audio plugin support** — lightweight C API, dlopen-based. Most realistic audio plugin standard to add beyond VST2. VST3/LV2/CLAP deferred — hosting SDKs are heavy and conflict with the lightweight footprint goal. (#12)
- **2.5D compositing** — per-layer Z-depth with perspective camera. Requires 3D projection matrix in `compose_sequence()`, Z-order per clip, camera node. Major architectural change. (#12)
- **Text animation** — letter-by-letter, word-by-word, line-by-line transforms + typewriter effect. Requires a mini animation engine within the text effect. (#12)
- **2.5D motion tracker** — point/planar tracking with compositing integration. Requires optical flow or feature matching (CPU-bound). (#12)
- **Track-based alpha mask** — extension of the 2.0 image-source Alpha Mask: use a clip on the adjacent track as a dynamic alpha source (animated masks, video-as-mask). Requires reordering `compose_sequence()` so the mask clip renders into its own buffer without being drawn to the final target, plus a per-clip "is-mask" flag, plus edge-case handling (mask not active at playhead, effects on the mask, dimension mismatch). Revisit if demand confirms after 2.0 ships. (#61)
- **Sequence and master effect stacks** — effects applied to a whole sequence or to the master output instead of per clip. Olive 0.1 has no track or bus objects to hang them on. Until then: nest the sequence and apply the effects to the nested clip.

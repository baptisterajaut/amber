# What's new in Amber 2.0

Amber 2.0 is pre-alpha. The preview build is rebuilt from the `2.0.x` branch and published as the [Amber 2.0 Preview](https://github.com/baptisterajaut/amber/releases/tag/v2.0.0-preview) prerelease. For real projects, stay on 1.x, which keeps getting fixes. The features added in 1.6 and 1.7 are in both versions; what 2.0 has on top is listed below. 2.0 opens 1.x projects as they are.

## Only in 2.0

- **Ten new GPU effects**, replacing the most common Frei0r plugins with effects that run on the graphics card: Levels, Lift/Gamma/Gain, Tint, Threshold, Lens Correction, Find Edges (Olive's was broken and hidden), Colorize, Alpha Adjust, Glow and Denoise. They are keyframeable like any other effect and work on clips with transparency.
- **Curves effect** (in Color): tone curves edited right in Effect Controls. Click the curve to add a point and drag it, double-click a point or drag it out of the square to remove it; the end points set the black and white input and output levels. One curve for red, green and blue together, one for luminance (keeps the colours' hue, lifts blacks without coloured speckle) and one per channel. Each change is one undo step.
- **Alpha Mask effect** (in Keying): cuts the clip to the shape of an image file (PNG, JPEG, WebP, BMP or SVG). The clip stays where the image is opaque, or where it is white with the Luminance channel, so a black-on-transparent logo and a white-on-black stencil both work. Stretch, Fit or Fill the image to the clip, then move, scale, feather or invert it, or lower its opacity, all keyframeable: shape stencils, logo-shaped windows, text reveals. The mask belongs to the clip and moves with its Transform. A missing or unreadable file leaves the clip unchanged.
- **Frei0r effects are hidden** from the effect menus by default: they run on the CPU and slow playback, and the new GPU effects cover the common ones. Preferences > Behavior > "Show Frei0r effects (CPU, slower)" lists them again, and a project that already uses Frei0r effects shows them while it is open. Their projects open and render as before.
- **Half resolution while scrubbing**, in Preferences > Playback (Auto by default): when the viewer can't keep up while you drag the playhead, scrub frames render at half the preview resolution, and the frame is redrawn sharp a moment after you stop. Always and Never are there too.
- **Undo history with times**: each step in the Undo History panel shows when it was done. Clicking a step goes back to it.
- **Timer and Progress Bar generators**, in the timeline's Add menu (and in the effect list, under Render): a timer that counts up from the clip's start or down to 0 at its end, in minutes:seconds, hours, seconds or hundredths, with an optional prefix; and a bar along the bottom or top edge that fills over the clip's duration or follows a keyframeable value. Trimming the start of the clip doesn't make either jump.
- **Drop Shadow effect** (under Stylize): the shadow is cast by the clip as it appears in the frame, so a scaled-down opaque video (picture-in-picture) gets one too, and it follows Corner Pin and keyed shapes. Colour, opacity, direction, distance and softness, all keyframeable; it looks the same in the viewer at any preview resolution and in the export.
- **ShaderToy shaders as effects**: *Import ShaderToy Shader...* at the end of the Add Video Effect menu takes a name and the shader's "Image" tab source, checks that it compiles (the error shows in the dialog, with your line numbers), saves it to your effects folder and adds it to the selected clips. A `.shadertoy` file dropped into an effects folder shows up under the ShaderToy category at the next launch. `iChannel0` is the clip, `iTime` counts from the clip's start, `iResolution` is the clip's size. To get sliders, put a `<name>.xml` next to `<name>.shadertoy`: an effect XML whose `<shader frag="<name>.shadertoy"/>` and whose fields become uniforms (only a sidecar with the same name replaces the plain entry; another name gives two entries). Limits of this first version: one pass and one input (`iChannel0`), the shader can't change the clip's transparency (it draws where the clip is opaque, a generator needs an opaque clip under it), `iDate` is the wall clock (an export that uses it changes each time), and a project refers to the shader by name, so send the `.shadertoy` file with the project. A shader that fails to compile shows a warning icon on its effect with the error and leaves the clip unchanged.
- **Scopes panel**, in Window > Scopes: a luma waveform, RGB parade, histogram or vectorscope (with 75% colour bar targets and a skin-tone line) of the sequence viewer's frame, updated as you scrub or play. It costs nothing while hidden or behind another tab.
- **Frame rate overlay**, in View > Show Frame Rate: while the sequence viewer plays, its top-left corner shows how many frames per second it actually displays next to the sequence's rate, e.g. `29.7 / 29.97 fps`, in red when playback can't keep up (below 95% over a full second, however slow: a frame every two seconds reads `0.5 / 29.97 fps`). It stays through a loop and disappears when playback stops.
- **Pixel sampler**, in View > Show Pixel Sampler: hovering either viewer shows the colour under the pointer in its bottom-left corner, as a swatch and as `X 812  Y 403   R 210  G 180  B 96  A 255`, the position in sequence pixels and the 0-255 values an export would write there. The eyedropper now picks the right pixel when the viewer is zoomed in past its size.
- **Use Proxies switch**, in the viewer's preview resolution menu (the Full / 1/2 / 1/4 button) and in View > Use Proxies: unticking it plays the original files of footage that has a proxy, right away and even during playback, without regenerating anything; ticking it goes back to the proxies. It is on by default and remembered. Export always uses the original files; Save Frame saves what the viewer shows.
- **Viewer overlays when zoomed in**: past the viewer's size, guides, Transform and Corner Pin handles, the title safe area and the rulers stay on the picture as you scroll (they were drawn upside down relative to the scroll, or on the visible area instead of the frame), and clicking or dragging a guide or a handle grabs the one under the pointer and moves it at the pointer's pace.
- **Gradient text**: the Gradient effect's new "As Mask" option fills the letters of the Text or Rich Text effect above it with the gradient. "Ignore Text Shadow" keeps the shadow in its own colour.
- **Richer clip tooltip**: hovering a clip also shows its source file, speed and colour label.
- **Vertical sequence presets**: 4K, 1080p and 720p portrait in New Sequence, for phone video.
- **Empty-state hints**: an empty Project panel says to double-click to import, and an empty timeline says to drag clips in to create a sequence. Dropping files still works in both.
- **Action search** ranks your most-used commands first and lists every action before you type.
- **Double-click a Title or Rich Text clip** in the timeline to edit its text.
- **Status bar hints** on menu and tool actions.
- **Save As** proposes the current file name, or a timestamped name for a project that was never saved.
- **Re-open the last project on startup**, in Preferences > Behavior (off by default).
- **Middle-click edge scrolling** in the timeline, in Preferences > Behavior (off by default; middle-drag still pans).
- **Linked clips move across tracks together**: dragging a video clip up to V2 takes its linked audio down to A2, and dragging the audio down takes the video up, as in other editors. This applies to every selected clip on the other side, linked or not. When one of them is already next to the video/audio boundary and can't go further, the drag only moves the clips in time. Alt-click still selects a clip without its links.
- **Thinner tracks**: tracks can shrink to 15 px instead of 30 (#73).
- **Timeline ruler**: timecode labels no longer overlap, and dragging a marker to the edge scrolls the view.
- **Clip names** are drawn with a shadow, readable on any clip colour.
- **Scrubbing in Effect Controls** repaints the panel instead of rebuilding it.
- **Exports with transparency** (PNG, QuickTime Animation, ProRes 4444 and the other formats with alpha, picked in the advanced video settings) and Save Frame as PNG keep semi-transparent areas at their real colour: a half-transparent white clip used to come out half-transparent grey. Exports without transparency are unchanged. The fix is coming to 1.x too.
- **Bezier into Hold keyframes**: a Bezier keyframe followed by a Hold keyframe now eases with the Bezier keyframe's handle. Projects that use this combination animate differently than in 1.x.
- **Preferences** describe what each option does in its tooltip.
- **Translations**: the 2.0 strings are translated in all 15 languages, and about 600 existing entries were corrected (wrong meanings such as Import shown as Export, swapped Move Up / Move Down, blank labels, typos).

## Fixes (coming to 1.x too)

- **Export reads the original media**: footage with a finished proxy was exported from the proxy, at its reduced resolution and quality. Proxies are now used for preview only; export (video and audio) always decodes the original file, and the viewer goes back to the proxy after the export.
- **Frei0r effects on proxied footage**: a Frei0r effect silently did nothing on a clip whose footage had a proxy. It now processes the frame it is given, whatever its size, and no longer shears the picture of footage of some widths, such as 1366 pixels or a 270-pixel-wide proxy of portrait video.
- **GIF export with an adaptive palette**: choosing pal8 for a GIF in the advanced video settings crashed the export on its first frame. It now works and gives each frame its own palette of up to 256 colours taken from the picture, with transparency kept, instead of the fixed 256-colour grid of the default rgb8. On 1.x the pal8 option is removed instead, and a pixel format the converter can't produce stops the export with an error rather than a crash.

## Under the hood

- The engine is separated from the interface and builds as a library of its own, so rendering can be tested without a window.
- A GPU test suite renders about 220 effect and decode cases on a real Qt RHI backend (OpenGL and Vulkan), to catch rendering regressions before they ship.
- Rendering reuses its GPU pipelines and buffers from one frame to the next instead of recreating them: a frame with effects composites about a third faster on OpenGL.
- Decoded video frames are copied into buffers that each clip keeps, instead of new memory every frame.
- The timeline drawing and mouse handling code was split into much smaller functions. This is the part of 2.0 most likely to hide regressions: if something in the timeline behaves differently from 1.x, please open an issue.
- Frei0r plugins no longer have their functions looked up again on every frame.

## Not there yet

Planned for 2.0 and not started: track mute/solo/lock, a colour correction tool, adjustment layers, hardware encoding and a render queue. The full plan is in the [roadmap](https://github.com/baptisterajaut/amber/blob/main/ROADMAP.md).

There is no Arch package for 2.0 yet; use the AppImage.

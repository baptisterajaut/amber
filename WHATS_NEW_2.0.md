# What's new in Amber 2.0

Amber 2.0 is pre-alpha. The preview build is rebuilt from the `2.0.x` branch and published as the [Amber 2.0 Preview](https://github.com/baptisterajaut/amber/releases/tag/v2.0.0-preview) prerelease. For real projects, stay on 1.x, which keeps getting fixes. The features added in 1.6 and 1.7 are in both versions; what 2.0 has on top is listed below. 2.0 opens 1.x projects as they are.

## Only in 2.0

- **Undo history with times**: each step in the Undo History panel shows when it was done. Clicking a step goes back to it.
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
- **Thinner tracks**: tracks can shrink to 15 px instead of 30 (#73).
- **Timeline ruler**: timecode labels no longer overlap, and dragging a marker to the edge scrolls the view.
- **Clip names** are drawn with a shadow, readable on any clip colour.
- **Scrubbing in Effect Controls** repaints the panel instead of rebuilding it.
- **Preferences** describe what each option does in its tooltip.
- **Translations**: the 2.0 strings are translated in all 15 languages, and about 600 existing entries were corrected (wrong meanings such as Import shown as Export, swapped Move Up / Move Down, blank labels, typos).

## Under the hood

- The engine is separated from the interface and builds as a library of its own, so rendering can be tested without a window.
- A GPU test suite renders about 70 effect and decode cases on a real Qt RHI backend, to catch rendering regressions before they ship.
- The timeline drawing and mouse handling code was split into much smaller functions. This is the part of 2.0 most likely to hide regressions: if something in the timeline behaves differently from 1.x, please open an issue.
- Frei0r plugins no longer have their functions looked up again on every frame.

## Not there yet

Planned for 2.0 and not started: GPU replacements for the common Frei0r effects, ShaderToy import, scopes, track mute/solo/lock, a colour correction tool, adjustment layers, hardware encoding and a render queue. The full plan is in the [roadmap](https://github.com/baptisterajaut/amber/blob/main/ROADMAP.md).

There is no Arch package for 2.0 yet; use the AppImage.

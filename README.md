# PixelForge

A native Linux image editor (C++17 / Qt 6) that reproduces the classic
Paint.NET 3.x workflow: floating translucent tool windows, the two-row toolbar with
a context-sensitive tool options bar, image thumbnails in the top-right corner,
layers with blend modes, unlimited history, adjustments and effects with live preview.

Only the UI and behaviour are modelled. No proprietary file formats, code or artwork
are used: all icons are drawn procedurally at runtime, and layered documents are
stored as **OpenRaster (.ora)**, the open format shared with GIMP, Krita and MyPaint.

## Build

Requirements: Qt 6 (Widgets, PrintSupport), zlib, CMake ≥ 3.20, a C++17 compiler.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/pixelforge [images...]
```

Install system-wide (binary + desktop entry): `sudo cmake --install build`.

Headless end-to-end test (drives the real tools with synthetic input, optional
screenshots):

```sh
QT_QPA_PLATFORM=offscreen ./build/pixelforge_selftest [screenshot-dir]
```

## What's in it

**Workspace** — floating Tools / Colors / History / Layers windows that go translucent
when the pointer leaves them, snap to edges, stay anchored to their corner on resize
(F5–F8 toggle, *Window → Reset Window Locations*); image thumbnail tabs with unsaved
marker, close button and dropdown list; rulers, pixel grid, units (px / in / cm);
zoom presets, *Zoom to Window*, *Zoom to Selection*, Ctrl+wheel zoom around the cursor,
Space or middle-button panning; status bar with tool hint, selection size, cursor
position and zoom.

**Tools** (shortcut letters cycle within a group; Shift cycles backwards)

| Key | Tools |
|-----|-------|
| S | Rectangle Select, Lasso Select, Ellipse Select, Magic Wand |
| M | Move Selected Pixels, Move Selection |
| Z / H | Zoom (click, right-click, drag a rectangle) / Pan |
| F / G | Paint Bucket (contiguous/global, tolerance, hatch fills) / Gradient (linear, reflected, diamond, radial, conical; color or transparency mode; editable handles) |
| B / E / P | Paintbrush / Eraser / Pencil |
| K | Color Picker (1×1…9×9 sampling, layer or image, after-pick action) |
| L / R | Clone Stamp (Ctrl+click sets source) / Recolor |
| T | Text (font, size, B/I/U/S, alignment, movable while editing) |
| O | Line/Curve (drag handles or the line to bend, Enter to finish), Rectangle, Rounded Rectangle, Ellipse, Freeform Shape |

Selection modifiers: Ctrl = add, right button = subtract, Ctrl+right = xor,
Alt = intersect, Shift = square/circle. Move Selected Pixels: drag to move, handles to
scale (Shift keeps aspect), right-drag to rotate (Shift snaps to 15°), arrow keys nudge,
switching tools records *Finish Pixels*. Left button paints with the primary color,
right button with the secondary. Antialiasing and Normal/Overwrite blending toggles
apply to every painting tool, and painting is clipped to the selection.

**Layers** — add, delete, duplicate, merge down, move up/down, import from file,
flip, rotate/zoom, properties (name, visibility, 14 blend modes, opacity) with live
preview; click the checkbox to hide/show, double-click for properties.

**Image** — crop to selection, resize (best quality / bilinear / nearest), canvas
size with 9-way anchor, flip, rotate 90°/180°, flatten.

**Edit** — undo/redo plus clickable history list; cut, copy, copy merged, paste (as a
movable floating selection, with an offer to expand the canvas), paste into new
layer / new image; select all, deselect, invert, erase and fill selection.

**Adjustments** — Auto-Level, Black and White, Brightness/Contrast, Curves,
Hue/Saturation, Invert Colors, Levels, Posterize, Sepia.

**Effects** — Artistic (Ink Sketch, Oil Painting, Pencil Sketch), Blurs (Fragment,
Gaussian, Motion, Radial, Unfocus, Zoom), Distort (Bulge, Frosted Glass, Pixelate,
Polar Inversion, Twist), Noise (Add Noise, Median), Photo (Glow, Red Eye Removal,
Sharpen, Soften Portrait, Vignette), Render (Clouds, Julia, Mandelbrot), Stylize
(Edge Detect, Emboss, Outline, Relief). Ctrl+F repeats the last effect.

**Files** — open anything Qt can read (PNG, JPEG, BMP, GIF, TIFF, WebP, ICO, …) and
.ora; save .ora (layers, blend modes, opacity) or flattened PNG/JPEG/BMP/TIFF/WebP
with a quality dialog; drag & drop; print.

## Source layout

| File | Purpose |
|------|---------|
| `src/document.*` | layers, blend modes, compositing cache, selection, snapshot history |
| `src/canvas.*` | zoomable view, rulers, grid, marching ants, zoom-out mip cache |
| `src/tools.*` | all 22 tools |
| `src/panels.*`, `src/colorpanel.*` | floating windows, tools/history/layers/colors panels |
| `src/effects.*` | adjustments/effects registry, live-preview dialog, curve editor |
| `src/dialogs.*` | new image, resize, canvas size, layer properties, rotate/zoom |
| `src/ora.*` | OpenRaster reader/writer (minimal zip + zlib) |
| `src/icons.*` | procedurally drawn icons and cursors |
| `src/mainwindow.*` | menus, toolbars, options bar, image tabs, commands |

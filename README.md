# DaVinci Resolve behaviour inquiry plugin

## Purpose

This plugin reproduces and visualises two questionable DaVinci Resolve
behaviours. Details are in the header comment of
[InquiryPlugin.cpp](InquiryPlugin.cpp).

1. **When a generator clip is placed inside an in/out range, `render()` always
   receives `time = 0`.**
   The rectangle's size is derived solely from `RenderArguments::time`, so it
   normally changes as the clip plays. However, once in/out points are set and a
   clip is placed within that range, the size no longer changes during playback.
   This is because `render()` is always handed `time = 0` instead of the clip's
   actual timeline frame.

2. **After a cut, a keyframe set from the plugin lands at the wrong position.**
   After cutting a clip into two, press the button on the *second* clip to set a
   keyframe at frame 0 via `setValueAtTime(0.0, 1.0)`. The keyframe should land
   on the second clip's own frame 0, but instead it is placed at frame 0 of the
   first (pre-split) clip.

## Build

The only dependency is the SDK bundled with DaVinci Resolve (OpenFX-1.4 headers +
Support sources). It builds exactly like the OpenFX sample `GainPlugin`: simply
compile `InquiryPlugin.cpp` together with the OFX Support library sources.

## Install (Windows)

Copy the whole `InquiryPlugin.ofx.bundle` directory to:

```
C:\Program Files\Common Files\OFX\Plugins
```

It then appears as **Inquiry Rect** under the "Resolve Inquiry" group, in both
the generator and effect lists.

# myk-audio-editor Non-Destructive Editor App 

I confess I did not write a single line of C++ for this program. Just trying out CODEX in VSCODE.

It turned out quite well so I'm dumping it here. 

<img width="1031" height="698" alt="image" src="https://github.com/user-attachments/assets/d9748059-b246-445e-bd59-bbd9874f97a4" />


# Build it

```
git clone git@github.com:yeeking/myk-audio-editor.git
cd libs 
git clone --recurse-submodules https://github.com/Tracktion/tracktion_engine.git
cd ..
cmake -B build .
cmake --build build --config Release -j 20 # change j based on your CPU cores/ general level of enthusiasm
```

# Usage Guide

## Getting started
- **Load audio**: Click `Load Audio` or press `Cmd+O`, pick a file (WAV/AIFF/FLAC/OGG/etc.).
- **Play/Pause**: Press `Space` or click `Play`. Playhead is shown on the waveform.
- **Plugins**: `Add Plugin` inserts a plugin on the track.
- **Export**: Click `Export` to choose format/rate/bit depth/compression options, then pick a destination file.

## Mouse controls (waveform)
- **Left click**: Move playhead to the click position.
- **Left drag**: Create a selection; selection overlays on the waveform.
- **Resize selection**: Hover near start/end of the selection (cursor becomes left-right resize), then drag to adjust that edge.
- **Right click**: Move playhead only (does not change the selection).
- **Mouse wheel**: Zoom in/out around the playhead (view auto-centers during playback if zoomed in).

## Keyboard shortcuts
- `Space`: Play/Pause.
- `Left / Right`: Scrub playhead backward/forward with audio; speed accelerates while held (up to ~3×).
- `Home`: Jump playhead to selection start, or file start if none.
- `End`: Jump playhead to selection end, or file end if none.
- `Page Down`: Zoom in around playhead.
- `Page Up`: Zoom out.
- `Cmd+O`: Open audio file.
- `Cmd+C / Cmd+X / Cmd+V`: Copy / Cut / Paste selection (ripple-aware).

## Selection & editing
- Selections define the region for copy/cut/paste and for moving the playhead via `Home`/`End`.
- Copy/Cut respects assembled segments; Paste inserts clipboard at the playhead (ripple).
- Playhead snaps to selection start after completing a drag selection.

## Export options
- Formats: WAV, AIFF, FLAC, OGG, MP3, M4A (visible options change per format).
- PCM formats: choose sample rate and bit depth; dithering is auto-enabled for non-float where appropriate.
- OGG: choose quality.
- MP3/M4A: choose bitrate.
- After setting options, a save dialog appears to choose the destination file.

# audiofx — live audio-reactive FPP plugin

Captures a **live USB audio input**, analyzes it in real time (level, frequency
bands, beat), and modulates the channel data each frame so the lights react to
the room's audio. A ChannelData modifier like `pixelfx` — never touches test
patterns, optional "only while a sequence is playing", settings apply live.

Compatible with FPP **5.4 → 9.x** (one source; ALSA capture is Linux/device-only,
compiles as a no-op stub elsewhere).

## How it works

```
 USB audio ─► ALSA capture (AlsaCapture) ─► AudioAnalyzer (kiss_fft)
                                              level · bands · beat · bpm
                                                        │
                                          AudioFxPlugin::modifyChannelData
                                                        │
                                   reactions over your channel range
```

The capture runs in a background thread (robust to device hot-plug / xruns). The
analyzer does a windowed real FFT, log-spaced band energies, RMS level (AGC),
and spectral-flux beat detection. A live snapshot is written to
`/tmp/audiofx_status.json` for the settings-page meters.

## Roadmap

- **Phase 1 ✅** capture + analysis + live meters + `level → brightness`, `beat → flash`.
- **Phase 2 ✅** spectrum/VU visualizer (`vis_mode`) + spectral hue drive (`hu_*`).
- **Phase 3 ✅** `level/beat → speed` (modulates FPP's playback rate; light-only
  sequences) + BPM tuning + gain/gate/sensitivity.
- **Phase 4 (optional):** split capture into a sidecar daemon for crash isolation.

The full reaction pipeline (each independently toggleable, over your channel
range): visualizer → hue drive → level→brightness → beat→flash, plus the
audio→speed lever. AGC + a noise gate keep it working across quiet and loud rooms.

## Setup

1. Plug in the USB audio interface. Find its ALSA device: `arecord -l` →
   e.g. `hw:1,0`.
2. Build/install (it links `-lasound`, bundles the FFT — no extra deps):
   `make` on the device, or install via the FPP plugin manager.
3. **Content Setup → Audio FX**: pick the device (the dropdown lists capture
   devices), watch the **Live monitor** meters move, then enable the plugin and
   the reactions. Set the channel range to cover your pixels.

## Settings (`config/plugin.audiofx`)

| Group | Keys |
|---|---|
| general | `enabled`, `onlyWhenPlaying` |
| audio | `audioDevice`, `sampleRate`, `gain`, `gate`, `sensitivity` |
| range | `channelsPerPixel`, `startChannel`, `channelCount` |
| reactions | `br_enabled`, `br_min`, `fl_enabled`, `fl_intensity` |

Full machine-readable schema (types, ranges, sliders) is the `settingsSchema` in
[`pluginInfo.json`](pluginInfo.json), so a companion app can auto-render it and
drive it over `POST /api/plugin/audiofx/settings/<key>`.

Bundled `src/kissfft/` is Mark Borgerding's kiss_fft (BSD/3-clause).

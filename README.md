# Pixel Pulse — live audio-reactive lighting for FPP

**Pixel Pulse** turns a live audio input into real-time lighting. It captures a
USB sound card on your Falcon Player (Raspberry Pi / BeagleBone), analyzes the
sound as it happens — loudness, frequency content, and beats — and modulates the
pixel data each frame so your lights move with the music in the room. No
pre-rendered audio, no cloud: all the analysis runs on the device.

It is a companion to [`pixelfx`](https://github.com/joeskolengaden/pixelfx): a
`ChannelData` modifier that layers on top of whatever is playing, never alters
test patterns, and applies settings live (no `fppd` restart to tweak).

> Why a separate plugin? xLights bakes audio reactions into the `.fseq` at
> *design time* — at playback there is no live audio. Reacting to **live** sound
> (a band, a DJ, a mic, a line feed) has to be done on the device, which is
> exactly what Pixel Pulse does.

- **FPP 5.4 → 9.x**, one source (the ALSA capture is Linux/device-only and
  compiles as a no-op stub elsewhere).
- **Self-describing**: every setting is in `pluginInfo.json`'s `settingsSchema`,
  so a companion app can auto-render it and drive it over REST.

---

## What it does

| Reaction | Setting group | Effect |
|---|---|---|
| **Level → brightness** | `reactions` | The design's brightness follows loudness — it breathes with the music. |
| **Beat → flash** | `reactions` | A white flash punches on every detected beat. |
| **Spectrum / VU visualizer** | `visualizer` | Generates pixels *directly* from the audio over your range — a VU bar or a full spectrum analyzer across the strand. |
| **Spectral hue drive** | `visualizer` | Rotates the design's hue with the bass↔treble balance. |
| **Audio → speed** | `speed` | Modulates FPP's playback rate from the level or beat (light-only sequences). |

All reactions are independently toggleable and run over a channel range you
choose, so you can react on one prop and leave the rest alone.

---

## How it works

```
 USB audio ─► ALSA capture (thread) ─► AudioAnalyzer (kiss_fft)
                                         level · bands · beat · bpm
                                                   │  (lock-free atomics)
                                       AudioFxPlugin::modifyChannelData
                                                   │
                     visualizer → hue drive → brightness → flash  +  audio→speed
                                                   │
                                          your channel range
```

- **Capture** (`AlsaCapture`): a background thread reads PCM from the ALSA
  capture device, mixes to mono, and feeds the analyzer. It is robust to
  hot-plug and xruns — if the device isn't there it retries quietly and reports
  "no audio device" rather than failing.
- **Analysis** (`AudioAnalyzer`): a Hann-windowed real FFT (bundled
  [kiss_fft](https://github.com/mborgerding/kissfft)) with 50 % overlap, giving:
  - **level** — RMS loudness, auto-gain-controlled to 0–1
  - **bands** — log-spaced band energies (8 by default), AGC'd, plus grouped
    bass / mid / treble
  - **beat** — a decaying onset envelope from spectral flux with an adaptive
    threshold and a refractory period
  - **bpm** — tempo estimate from beat intervals
- **Reaction** (`modifyChannelData`): reads the live features and modulates the
  channel buffer each frame.

A snapshot is written to `/tmp/pixelpulse_status.json` ~8×/sec so the settings
page can show **live meters** while you set up.

The DSP is pure (no I/O) and unit-tested: a 60 Hz tone reads `bass = 1.0`, and
beats every 0.5 s are detected at ~120 BPM.

---

## Setup

1. **Plug in** the USB audio interface and find its ALSA device:
   ```bash
   arecord -l
   ```
   e.g. `card 1: Device [USB Audio], device 0:` → device id **`hw:1,0`**.
2. **Install** — either:
   - **FPP UI:** Content Setup → Plugins → paste the raw `pluginInfo.json` URL:
     `https://raw.githubusercontent.com/joeskolengaden/pixelpulse/main/pluginInfo.json`
     → Get Plugin Info → Install → restart. *(On FPP 5.x use the raw URL, not
     the `blob` page URL.)*
   - **Manual:** copy the folder to `/home/fpp/media/plugins/pixelpulse/`, run
     `sudo ./scripts/fpp_install.sh`, then `sudo systemctl restart fppd`.
   The build links `-lasound` (already on FPP) and bundles the FFT — no extra
   packages.
3. **Configure** — Content Setup → **Pixel Pulse**:
   - Pick the **device** (the dropdown lists capture devices from `arecord -l`).
   - Watch the **Live monitor** meters move — confirm it's hearing sound.
   - Turn on the plugin and the reactions; set the **range** to cover your pixels.

---

## Settings reference

Stored in `config/plugin.pixelpulse` (key=value); edit via the UI or
`POST /api/plugin/pixelpulse/settings/<key>`. Changes apply within ~0.5 s.

| Key | Type | Default | Meaning |
|---|---|---|---|
| `enabled` | 0/1 | 0 | Master on/off |
| `onlyWhenPlaying` | 0/1 | 1 | Only react while a sequence plays (test patterns never touched) |
| `audioDevice` | string | `default` | ALSA capture device, e.g. `hw:1,0` |
| `sampleRate` | 44100/48000 | 44100 | Capture sample rate |
| `gain` | float | 1.0 | Input gain (pre-analysis) |
| `gate` | float | 0.02 | Noise gate — below this is silence |
| `sensitivity` | float | 1.5 | Beat threshold (lower = more beats) |
| `channelsPerPixel` | 3/4 | 3 | RGB or RGBW |
| `startChannel` / `channelCount` | int | 1 / 1500 | Channel range to affect |
| `br_enabled` / `br_min` | 0/1, % | 1 / 15 | Level → brightness, with a floor |
| `fl_enabled` / `fl_intensity` | 0/1, % | 1 / 80 | Beat → white flash |
| `vis_mode` | off/vu/spectrum | off | Direct visualizer (overrides the range) |
| `hu_enabled` / `hu_amount` | 0/1, deg | 0 / 60 | Spectral hue drive |
| `speed_mode` | off/level/beat | off | Audio → playback speed |
| `speed_amount` | % | 50 | Max speed-up at full level/beat |

Full machine-readable schema (types, ranges, sliders, groups) is the
`settingsSchema` array in [`pluginInfo.json`](pluginInfo.json).

---

## Notes, limits & tuning

- **Speed reactions are for light-only sequences.** A show with embedded audio is
  slaved to its media clock, so changing the playback rate would desync it. Use
  `speed_mode` on animation / light-only sequences (or a live-performance setup
  where the music isn't part of the `.fseq`).
- **AGC + noise gate** keep it working across quiet and loud rooms. If it never
  reacts, lower the **gate** or raise the **gain**; if it flashes on everything,
  raise the **sensitivity**.
- **Performance** is light — a 1024-point FFT at ~50 Hz is well under 5 % of one
  BeagleBone core. The visualizer and per-pixel hue are the heaviest; narrow the
  range if you stack everything on a huge prop.
- **Output frame-rate ceiling** still applies (≈30 fps on 1020-pixel ports); the
  reactions update as fast as FPP outputs frames.

---

## Build from source

```bash
make                      # -> libpixelpulse.so  (.dylib on macOS, ALSA stubbed)
make FPPDIR=/path/to/fpp  # if FPP isn't at /opt/fpp
```

`callbacks.sh` makes FPP load the compiled library; `scripts/fpp_install.sh`
builds on install.

## Roadmap

- **Phase 1 ✅** capture + analysis + live meters + level→brightness, beat→flash
- **Phase 2 ✅** spectrum/VU visualizer + spectral hue drive
- **Phase 3 ✅** audio→speed + BPM tuning + gain/gate/sensitivity
- **Phase 4** (optional) split capture into a sidecar daemon for crash isolation

## Credits & license

Bundled `src/kissfft/` is Mark Borgerding's **kiss_fft** (BSD 3-clause).
Plugin © joeskolengaden.

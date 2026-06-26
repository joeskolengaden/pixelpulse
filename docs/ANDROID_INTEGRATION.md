# Controlling FPP plugins from an Android app

This guide explains how to build an Android (or any) app that discovers an
**[Falcon Player (FPP)](https://github.com/FalconChristmas/fpp)** controller on the
local network and drives its plugins — including
**[Pixel Pulse Audio](https://github.com/joeskolengaden/pixelpulse)** and any other
plugin that follows the lightweight convention described below.

You write **no plugin-side code in the app**. FPP exposes everything over a plain
HTTP REST API; the app is just an HTTP client.

---

## 1. The big picture

```
┌────────────┐    HTTP/JSON over LAN     ┌──────────────────────────────┐
│ Android app│ ────────────────────────▶ │ FPP controller (e.g. BBB/Pi) │
│ (HTTP client)│  GET/POST :80            │  - REST API (/api/...)        │
└────────────┘ ◀──────────────────────── │  - Plugins (settings + status)│
                                          └──────────────────────────────┘
```

Three layers of control, from most generic to richest:

| Layer | Works for | What you get |
|---|---|---|
| **Universal settings API** | *every* plugin, no changes | raw read/write of any setting (key/value) |
| **`app.php` convention** | our plugins (Pixel Pulse, …) | one call → labelled controls + live status |
| **WebView fallback** | third-party plugins | embed the plugin's existing web page |

---

## 2. Connecting to a controller

- **Base URL:** `http://<controller-ip>/` (port 80, plain HTTP on the LAN).
- **Discovery (recommended):** FPP advertises itself via **mDNS/Bonjour** — browse for
  `_fpp._tcp` (and `_http._tcp`). Android: `NsdManager`.
  You can also let the user type the IP, or scan the subnet.
- **Find other FPP devices from one of them:** `GET /api/fppd/multiSyncSystems`.
- **Auth:** a default FPP install is open on the LAN. If the user enabled a UI
  password, send HTTP **Basic/Digest** auth. **Keep this LAN-only — never expose FPP
  to the internet; use a VPN for remote access.**
- **Health/identity:** `GET /api/fppd/version`, `GET /api/system/status`
  (the latter includes `advancedView.Utilization.CPU`, memory, platform, host name).

---

## 3. The recommended path: the `app.php` endpoint

Every plugin that ships a `pluginInfo.json` schema can include a small, **reusable**
`app.php` that returns *everything the app needs in one call*:

```
GET http://<ip>/plugin.php?plugin=<repoName>&page=app.php&nopage=1
```

### Response shape (the contract)

```json
{
  "plugin": "pixelpulse",
  "name":   "Pixel Pulse Audio",
  "type":   "channelData",
  "groups": ["general", "spatial", "audio", "tuning", "switch"],
  "settings": [
    {
      "key":     "spatial_autocycle",
      "label":   "Auto design change",
      "type":    "enum",            // bool | int | enum | string
      "options": ["off","time","beats","smart"],
      "group":   "spatial",
      "help":    "smart = pick designs to match the music",
      "value":   "smart"           // current value (merged from the live config)
    },
    {
      "key": "min_glow", "label": "Minimum glow", "type": "int",
      "min": 0, "max": 50, "step": 1, "unit": "%", "widget": "slider",
      "group": "spatial", "value": "8"
    }
    // ... one object per setting ...
  ],
  "status": {                       // live telemetry, or null if the plugin has none
    "level": 0.62, "bass": 0.5, "mid": 0.46, "treble": 0.34,
    "beat": 0.0, "bpm": 120, "tempoConf": 0.73,
    "spatialMode": "bars", "musicType": "groove",
    "deviceOk": true, "bands": [ ... ]
  },
  "setUrl": "api/plugin/pixelpulse/settings/"   // POST <key> here with the value as the body
}
```

### Rendering it (one generic screen for all plugins)
1. Group `settings` by their `group` field → one section per group.
2. Draw one control per setting **by `type`**:
   - `bool` → switch
   - `int` (+ `min`/`max`/`step`/`unit`, optional `widget:"slider"`) → slider / stepper
   - `enum` (+ `options`) → dropdown / segmented control
   - `string` → text field
3. Seed each control with `value`.
4. On change, **POST the new value** (see §5).
5. Poll `status` (or `status.php`, §6) on a timer to animate meters / show BPM / current design.

Because the screen is generated from the schema, **adding a new setting to the
plugin makes it appear in the app automatically** — no app release needed.

> The same `app.php` file works in any plugin: it reads its own `pluginInfo.json`
> (schema), the plugin's config file (current values), and an optional
> `/dev/shm/<repoName>_status.json` (live status). To make another plugin
> app-native, give it a `pluginInfo.json` with a `settingsSchema` and drop in the
> same `app.php`.

---

## 4. The universal settings API (works for any plugin)

If you'd rather not rely on `app.php`, or for plugins that don't ship one:

```
GET  /api/plugins                                  → ["pixelpulse","credits", ...]
GET  /api/plugin/<repo>/settings/<key>             → {"<key>":"<value>"}
POST /api/plugin/<repo>/settings/<key>             body: <new value>      (set one)
GET  /api/configfile/plugin.<repo>                 → raw config (all keys)
```

The schema itself lives in the plugin's `pluginInfo.json` under `settingsSchema`
(array of `{key,label,type,default,options,min,max,group,help,...}`). Fetch it
once to know how to render the controls; fetch the config for current values.

---

## 5. Writing a setting

```
POST http://<ip>/api/plugin/pixelpulse/settings/spatial_autocycle
Content-Type: text/plain
Body: smart
```
(Equivalent legacy form the web UI uses:
`GET /fppjson.php?command=setPluginSetting&plugin=pixelpulse&key=...&value=...`)

**Apply semantics:**
- Most settings **apply live within ~0.5 s** while the plugin is rendering.
- A few settings — notably the **audio device / sample-rate / input channel** —
  only take effect on a clean reload. After changing those, call:
  ```
  GET http://<ip>/api/system/fppd/restart      // restarts ONLY the fppd daemon (~5–8s), not a reboot
  ```
  Surface a **"Save & Restart fppd"** button for this. (`/api/system/reboot` is a
  full device reboot — don't use it for settings.)

---

## 6. Live status (meters / BPM / current design)

Poll this 3–5×/sec to drive a live monitor (Pixel Pulse example):

```
GET http://<ip>/plugin.php?plugin=pixelpulse&page=status.php&nopage=1
→ { "deviceOk":true, "active":true, "level":0.62, "beat":0.0,
    "bass":0.5, "mid":0.46, "treble":0.34, "bpm":120, "tempoConf":0.73,
    "beatPhase":0.31, "onsets":1840, "rawLevel":0.08,
    "spatialMode":"bars", "musicType":"groove",
    "switchEnabled":false, "switchOn":true, "bands":[ ... ] }
```

Field notes: `level/bass/mid/treble/bands` are 0..1 meters; `bpm` is the locked
tempo; `tempoConf` 0..1 is how confident the lock is; `onsets` is a monotonic beat
counter (rate = diff over time); `spatialMode`/`musicType` are the current design
and detected genre.

---

## 7. Live pixel preview (optional)

To draw the actual output on a Canvas (like the web preview):

```
GET http://<ip>/plugin.php?plugin=pixelpulse&page=frame.php&nopage=1
→ "<count>\n<hex...>"      // a downsampled set of LEDs, RRGGBB per pixel
```
Pair it with the layout point positions for an aspect-correct preview:
```
GET http://<ip>/uploadlayout.php?plugin=pixelpulse&points=1   // nx,ny,dist,mask + aspect ratio + groups
```

---

## 8. Playback & system commands

```
POST /api/command   {"command":"Start Playlist","args":["<name>","false","true"]}
POST /api/command   {"command":"Stop Now"}
GET  /api/playlists                         → playlist names
GET  /api/sequence                          → sequence names
GET  /api/fppd/status                       → player state (status_name, current_playlist, ...)
GET  /api/system/fppd/restart               → restart the fppd daemon
GET  /api/system/status                     → CPU/mem/host (advancedView.Utilization.CPU)
```

Pixel Pulse specifics:
```
GET /plugin.php?plugin=pixelpulse&page=devices.php&nopage=1            → list USB audio inputs
GET /plugin.php?plugin=pixelpulse&page=ambient.php&nopage=1&setup=1    → create/refresh the no-show ambient loop
```

---

## 9. Suggested Android architecture

- **Networking:** Retrofit + OkHttp (or Ktor) — native HTTP, no WebView/CORS issues.
- **Device model:** a controller = base URL (IP) + optional credentials; persist it.
- **Repository** that:
  1. loads `app.php` once → schema + current values (render the screen),
  2. polls `status.php` (or re-reads `app.php`) on a coroutine timer → live meters,
  3. POSTs to `setUrl + key` on each control change.
- **One generic `PluginControlScreen` (Compose):** takes the `settings` list and
  emits a `Switch` / `Slider` / `Dropdown` per `type`, grouped by `group`.
- **A live-monitor composable:** progress bars for level/bass/mid/treble, a BPM
  readout, the current design name, and (optionally) the frame canvas.

### Minimal Retrofit sketch (Kotlin)

```kotlin
interface FppApi {
    @GET("plugin.php")
    suspend fun app(@Query("plugin") p: String,
                    @Query("page") page: String = "app.php",
                    @Query("nopage") n: Int = 1): PluginApp

    @GET("plugin.php")
    suspend fun status(@Query("plugin") p: String,
                       @Query("page") page: String = "status.php",
                       @Query("nopage") n: Int = 1): Map<String, Any>

    // POST a single setting; value is the raw request body
    @POST("api/plugin/{repo}/settings/{key}")
    suspend fun setSetting(@Path("repo") repo: String,
                           @Path("key") key: String,
                           @Body value: RequestBody)

    @GET("api/system/fppd/restart") suspend fun restartFppd()
}

data class PluginApp(
    val plugin: String, val name: String, val type: String,
    val groups: List<String>, val settings: List<Setting>,
    val status: Map<String, Any>?, val setUrl: String
)
data class Setting(
    val key: String, val label: String?, val type: String,
    val options: List<String>?, val min: Int?, val max: Int?, val step: Int?,
    val unit: String?, val group: String?, val help: String?, val value: Any?
)

// set a value:  api.setSetting("pixelpulse","spatial_autocycle","smart".toRequestBody())
```

---

## 10. Gotchas & best practices

- **LAN-only.** FPP has no real auth model for the internet — keep it on the local
  network; use a VPN for remote control.
- **Restart for audio/capture changes.** Device/sample-rate/channel need
  `fppd/restart`; everything else is live. Tell the user which is which.
- **CPU-bound device.** A BeagleBone runs near its ceiling during a show; keep HTTP
  timeouts short, back off polling when backgrounded, and handle the offline case.
- **DHCP.** The controller's IP can change on reboot — prefer mDNS discovery, or
  re-resolve by host name, rather than hard-coding the IP.
- **Values are strings.** Config values come back as strings (e.g. `"8"`, `"smart"`,
  `"1"`); coerce by the schema `type`.
- **No CORS with native HTTP.** Only relevant if you use a WebView.

---

## 11. Endpoint quick-reference

| Purpose | Method | Endpoint |
|---|---|---|
| List plugins | GET | `/api/plugins` |
| **App blob (schema+values+status)** | GET | `/plugin.php?plugin=<repo>&page=app.php&nopage=1` |
| Read one setting | GET | `/api/plugin/<repo>/settings/<key>` |
| Write one setting | POST | `/api/plugin/<repo>/settings/<key>` (body = value) |
| All settings (raw) | GET | `/api/configfile/plugin.<repo>` |
| Live status | GET | `/plugin.php?plugin=<repo>&page=status.php&nopage=1` |
| Live frame | GET | `/plugin.php?plugin=<repo>&page=frame.php&nopage=1` |
| Audio inputs (Pixel Pulse) | GET | `/plugin.php?plugin=pixelpulse&page=devices.php&nopage=1` |
| Start/stop playback | POST | `/api/command` |
| Restart daemon | GET | `/api/system/fppd/restart` |
| Reboot device | GET | `/api/system/reboot` |
| System/CPU | GET | `/api/system/status` |
| Discover peers | GET | `/api/fppd/multiSyncSystems` |
| Version | GET | `/api/fppd/version` |

---

*Generated for the Pixel Pulse Audio plugin. The `app.php` convention is designed
to work across any FPP plugin that ships a `pluginInfo.json` schema.*

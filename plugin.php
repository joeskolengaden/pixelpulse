<?php
// Settings page for the "audiofx" plugin. $pluginSettings populated by FPP from
// config/plugin.audiofx. Styling scoped under #afx.
global $pluginSettings;
if (!isset($pluginSettings) || !is_array($pluginSettings)) $pluginSettings = array();
function af_get($k, $d = '') { global $pluginSettings; return isset($pluginSettings[$k]) ? $pluginSettings[$k] : $d; }
function af_chk($k, $d = '0') { return af_get($k, $d) == '1' ? ' checked' : ''; }
function af_js($k, $b = false) { $v = $b ? 'this.checked?1:0' : 'this.value'; $e = $b ? ' afxToggle(this);' : ''; return "SetPluginSetting('audiofx','$k',$v,0,0);$e"; }
function afNum($k, $d, $mn, $mx, $st, $u = '') { return "<div class=\"sl\"><input type=\"range\" min=\"$mn\" max=\"$mx\" step=\"$st\" value=\"" . htmlspecialchars(af_get($k, $d)) . "\" oninput=\"this.nextElementSibling.textContent=this.value+'$u';\" onChange=\"" . af_js($k) . "\"><span>" . htmlspecialchars(af_get($k, $d)) . "$u</span></div>"; }
function afInt($k, $d, $mn = '', $mx = '') { $a = ($mn !== '' ? " min=\"$mn\"" : '') . ($mx !== '' ? " max=\"$mx\"" : ''); return "<input type=\"number\"$a value=\"" . htmlspecialchars(af_get($k, $d)) . "\" onChange=\"" . af_js($k) . "\">"; }
function afTog($k, $d = '0') { return "<label class=\"sw\"><input type=\"checkbox\"" . af_chk($k, $d) . " onChange=\"" . af_js($k, true) . "\"><span class=\"sl2\"></span></label>"; }
?>
<style>
#afx{max-width:820px;margin:0 auto;color:#1f2733;font-size:14px}
#afx .intro{color:#6b7280;font-size:13px;margin:0 0 16px}
#afx .card{border:1px solid #e4e7ec;border-radius:12px;background:#fff;margin:0 0 14px;overflow:hidden}
#afx .head{display:flex;align-items:center;gap:10px;padding:12px 16px;background:#f6f8fa;border-bottom:1px solid #eceef2}
#afx .head .t{font-size:15px;font-weight:600;flex:1}
#afx .body{padding:14px 16px}
#afx .body.off{opacity:.45;pointer-events:none}
#afx .grid{display:grid;grid-template-columns:150px 1fr;gap:11px 16px;align-items:center}
#afx .lab{font-weight:500;color:#374151}
#afx .help{color:#6b7280;font-size:12.5px}
#afx input[type=number],#afx select{padding:7px 10px;border:1px solid #cdd3dc;border-radius:7px;background:#fff;font-size:14px;max-width:200px}
#afx .sl{display:flex;align-items:center;gap:11px;max-width:260px}
#afx .sl input[type=range]{flex:1;accent-color:#2f9e6f}
#afx .sl span{min-width:54px;text-align:right;font-weight:500;font-variant-numeric:tabular-nums}
#afx .sw{position:relative;display:inline-block;width:46px;height:25px}
#afx .sw input{opacity:0;width:0;height:0}
#afx .sw .sl2{position:absolute;cursor:pointer;inset:0;background:#cbd1da;border-radius:25px;transition:.18s}
#afx .sw .sl2:before{content:"";position:absolute;height:19px;width:19px;left:3px;top:3px;background:#fff;border-radius:50%;transition:.18s}
#afx .sw input:checked + .sl2{background:#2f9e6f}#afx .sw input:checked + .sl2:before{transform:translateX(21px)}
/* meters */
#afx .meter{display:flex;align-items:center;gap:10px;margin:7px 0}
#afx .meter .ml{width:62px;color:#374151;font-size:12.5px}
#afx .bar{flex:1;height:14px;background:#eceef2;border-radius:7px;overflow:hidden}
#afx .bar > div{height:100%;width:0%;border-radius:7px;transition:width .08s linear}
#afx .bands{display:flex;gap:4px;height:60px;align-items:flex-end;margin-top:8px}
#afx .bands > div{flex:1;background:#2f9e6f;border-radius:3px 3px 0 0;height:2%}
#afx .stat{display:flex;align-items:center;gap:8px;font-size:13px}
#afx .dot{width:11px;height:11px;border-radius:50%;background:#cbd1da}
#afx .dot.on{background:#2f9e6f}#afx .dot.beat{background:#e24b4a}
</style>

<div id="afx">
  <p class="intro">Live audio-reactive lighting. Pick your USB audio input, watch the meters confirm it's hearing sound, then turn on reactions. The lights modulate the playing design in real time (test patterns are never touched).</p>

  <div class="card">
    <div class="head"><span class="t">Live monitor</span><span class="stat"><span class="dot" id="af-dev"></span><span id="af-devtxt">checking…</span></span></div>
    <div class="body">
      <div class="meter"><span class="ml">Level</span><div class="bar"><div id="af-level" style="background:#2f9e6f"></div></div></div>
      <div class="meter"><span class="ml">Bass</span><div class="bar"><div id="af-bass" style="background:#e24b4a"></div></div></div>
      <div class="meter"><span class="ml">Mid</span><div class="bar"><div id="af-mid" style="background:#ef9f27"></div></div></div>
      <div class="meter"><span class="ml">Treble</span><div class="bar"><div id="af-treble" style="background:#378add"></div></div></div>
      <div class="meter"><span class="ml">Beat</span><span class="dot" id="af-beat"></span><span style="flex:1"></span><span style="font-size:12.5px;color:#6b7280">BPM <b id="af-bpm">–</b></span></div>
      <div class="bands" id="af-bands"></div>
      <div class="help" id="af-hint" style="margin-top:8px"></div>
    </div>
  </div>

  <div class="card">
    <div class="head"><span class="t">General</span><?php echo afTog('enabled'); ?></div>
    <div class="body"><div class="grid">
      <div class="lab">Only while playing</div><div><?php echo afTog('onlyWhenPlaying', '1'); ?></div>
    </div></div>
  </div>

  <div class="card">
    <div class="head"><span class="t">Audio input</span></div>
    <div class="body"><div class="grid">
      <div class="lab">Device</div><div><select id="af-device" onChange="SetPluginSetting('audiofx','audioDevice',this.value,0,0);"><option value="<?php echo htmlspecialchars(af_get('audioDevice', 'default')); ?>"><?php echo htmlspecialchars(af_get('audioDevice', 'default')); ?> (current)</option></select> <span class="help">USB capture device</span></div>
      <div class="lab">Sample rate</div><div><select onChange="SetPluginSetting('audiofx','sampleRate',this.value,0,0);"><?php foreach (array('44100','48000') as $r) echo "<option value='$r'" . (af_get('sampleRate','44100')===$r?' selected':'') . ">$r</option>"; ?></select></div>
      <div class="lab">Input gain</div><div><?php echo afNum('gain', '1.0', '0.1', '10', '0.1'); ?></div>
      <div class="lab">Noise gate</div><div><?php echo afNum('gate', '0.02', '0', '0.3', '0.005'); ?></div>
      <div class="lab">Beat sensitivity</div><div><?php echo afNum('sensitivity', '1.5', '1.05', '3', '0.05'); ?></div>
    </div></div>
  </div>

  <div class="card">
    <div class="head"><span class="t">Range</span></div>
    <div class="body"><div class="grid">
      <div class="lab">Channels / pixel</div><div><select onChange="SetPluginSetting('audiofx','channelsPerPixel',this.value,0,0);"><?php foreach (array('3','4') as $c) echo "<option value='$c'" . (af_get('channelsPerPixel','3')===$c?' selected':'') . ">$c</option>"; ?></select></div>
      <div class="lab">Start channel</div><div><?php echo afInt('startChannel', '1', 1); ?></div>
      <div class="lab">Channel count</div><div><?php echo afInt('channelCount', '1500', 0); ?> <span class="help">cover your pixels</span></div>
    </div></div>
  </div>

  <div class="card">
    <div class="head"><span class="t">Reactions</span></div>
    <div class="body"><div class="grid">
      <div class="lab">Level → brightness <?php echo afTog('br_enabled', '1'); ?></div><div class="help">Design brightness follows loudness.</div>
      <div class="lab">Min brightness</div><div><?php echo afNum('br_min', '15', '0', '100', '1', '%'); ?></div>
      <div class="lab">Beat → flash <?php echo afTog('fl_enabled', '1'); ?></div><div class="help">White flash on each beat.</div>
      <div class="lab">Flash intensity</div><div><?php echo afNum('fl_intensity', '80', '0', '100', '1', '%'); ?></div>
    </div></div>
  </div>

  <div class="card">
    <div class="head"><span class="t">Visualizer &amp; color</span></div>
    <div class="body"><div class="grid">
      <div class="lab">Visualizer</div><div><select onChange="SetPluginSetting('audiofx','vis_mode',this.value,0,0);"><?php foreach (array('off','vu','spectrum') as $m) echo "<option value='$m'" . (af_get('vis_mode','off')===$m?' selected':'') . ">$m</option>"; ?></select> <span class="help">generates pixels from the audio over the range</span></div>
      <div class="lab">Spectral hue drive <?php echo afTog('hu_enabled', '0'); ?></div><div class="help">Shift the design's hue with bass/treble balance.</div>
      <div class="lab">Hue amount</div><div><?php echo afNum('hu_amount', '60', '0', '180', '5', 'deg'); ?></div>
    </div></div>
  </div>
</div>

<script>
function afxToggle(cb){}
function afApi(p){ return 'plugin.php?plugin=audiofx&page=' + p + '&nopage=1'; }
// build band bars
var afBands = document.getElementById('af-bands');
for (var i=0;i<8;i++){ var d=document.createElement('div'); afBands.appendChild(d); }
// populate device list
fetch(afApi('devices.php')).then(function(r){return r.json();}).then(function(list){
  var sel=document.getElementById('af-device'); var cur=sel.value;
  (list||[]).forEach(function(dev){ if(dev.id===cur)return; var o=document.createElement('option'); o.value=dev.id; o.textContent=dev.id+' — '+dev.name; sel.appendChild(o); });
}).catch(function(){});
// live meters
function pct(v){ return Math.max(0,Math.min(100,Math.round(v*100)))+'%'; }
setInterval(function(){
  fetch(afApi('status.php')).then(function(r){return r.json();}).then(function(s){
    var dev=document.getElementById('af-dev'), txt=document.getElementById('af-devtxt');
    dev.className='dot'+(s.deviceOk?' on':''); txt.textContent = s.deviceOk ? (s.active?'hearing audio':'device open (silent)') : 'no audio device';
    document.getElementById('af-level').style.width=pct(s.level);
    document.getElementById('af-bass').style.width=pct(s.bass);
    document.getElementById('af-mid').style.width=pct(s.mid);
    document.getElementById('af-treble').style.width=pct(s.treble);
    document.getElementById('af-beat').className='dot'+(s.beat>0.5?' beat':(s.active?' on':''));
    document.getElementById('af-bpm').textContent = s.bpm>0 ? Math.round(s.bpm) : '–';
    var bars=afBands.children; (s.bands||[]).forEach(function(v,i){ if(bars[i]) bars[i].style.height=Math.max(2,Math.round(v*100))+'%'; });
    document.getElementById('af-hint').textContent = s.deviceOk ? '' : 'Tip: set the device (e.g. hw:1,0) and check it appears in the dropdown. Run "arecord -l" on the device to find it.';
  }).catch(function(){ document.getElementById('af-devtxt').textContent='(plugin not running — enable it and restart fppd)'; });
}, 130);
</script>

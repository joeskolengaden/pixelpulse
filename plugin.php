<?php
// Settings page for the "pixelpulse" plugin. $pluginSettings populated by FPP from
// config/plugin.pixelpulse. Styling scoped under #afx.
global $pluginSettings;
if (!isset($pluginSettings) || !is_array($pluginSettings)) $pluginSettings = array();
function af_get($k, $d = '') { global $pluginSettings; return isset($pluginSettings[$k]) ? $pluginSettings[$k] : $d; }
function af_chk($k, $d = '0') { return af_get($k, $d) == '1' ? ' checked' : ''; }
function af_js($k, $b = false) { $v = $b ? 'this.checked?1:0' : 'this.value'; $e = $b ? ' afxToggle(this);' : ''; return "SetPluginSetting('pixelpulse','$k',$v,0,0);$e"; }
function afNum($k, $d, $mn, $mx, $st, $u = '') { return "<div class=\"sl\"><input type=\"range\" id=\"afn-$k\" min=\"$mn\" max=\"$mx\" step=\"$st\" value=\"" . htmlspecialchars(af_get($k, $d)) . "\" oninput=\"this.nextElementSibling.textContent=this.value+'$u';\" onChange=\"" . af_js($k) . "\"><span id=\"afnv-$k\" data-u=\"$u\">" . htmlspecialchars(af_get($k, $d)) . "$u</span></div>"; }
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

  <div class="card" style="margin-bottom:14px"><div class="body" style="display:flex;align-items:center;gap:12px;flex-wrap:wrap;padding:12px 14px">
    <button id="af-saverestart" onclick="afSaveRestart(this)" style="background:#2f9e6f;color:#fff;border:0;border-radius:8px;padding:9px 16px;font-size:14px;font-weight:600;cursor:pointer">Save &amp; Restart fppd</button>
    <span style="font-size:12.5px;color:#6b7280">Settings save the moment you change them. Click here to restart only the fppd daemon and apply everything cleanly (needed after changing the audio device while nothing is playing). Not a reboot — the web UI stays up.</span>
    <span id="af-saverestart-stat" style="font-size:13px;color:#2f9e6f;font-weight:600"></span>
  </div></div>

  <div class="card">
    <div class="head"><span class="t">Live monitor</span><span class="stat"><span class="dot" id="af-dev"></span><span id="af-devtxt">checking…</span></span></div>
    <div class="body">
      <div id="af-monitor" style="display:flex;gap:16px;flex-wrap:wrap;align-items:flex-start">
        <div id="af-meters" style="flex:1 1 250px;min-width:230px">
          <div class="meter"><span class="ml">Level</span><div class="bar"><div id="af-level" style="background:#2f9e6f"></div></div></div>
          <div class="meter"><span class="ml">Bass</span><div class="bar"><div id="af-bass" style="background:#e24b4a"></div></div></div>
          <div class="meter"><span class="ml">Mid</span><div class="bar"><div id="af-mid" style="background:#ef9f27"></div></div></div>
          <div class="meter"><span class="ml">Treble</span><div class="bar"><div id="af-treble" style="background:#378add"></div></div></div>
          <div class="meter"><span class="ml">Beat</span><span class="dot" id="af-beat"></span><span style="flex:1"></span><span style="font-size:12.5px;color:#6b7280">BPM <b id="af-bpm">–</b></span></div>
          <div class="bands" id="af-bands"></div>
          <div class="help" id="af-hint" style="margin-top:8px"></div>
        </div>
        <div id="af-prevwrap" style="display:none">
          <div style="display:flex;gap:10px;flex-wrap:wrap;justify-content:center">
            <div>
              <div class="help" style="margin-bottom:4px">Playing now <span id="af-live-stat" style="opacity:.6"></span></div>
              <canvas id="af-preview-live" width="100" height="200" style="background:#0e1116;border-radius:8px;display:block;max-width:100%"></canvas>
            </div>
            <div>
              <div class="help" style="margin-bottom:4px">Audio effect · <b id="af-prevmode">bloom</b></div>
              <canvas id="af-preview-fx" width="100" height="200" style="background:#0e1116;border-radius:8px;display:block;max-width:100%"></canvas>
            </div>
          </div>
        </div>
      </div>
    </div>
  </div>

  <div class="card">
    <div class="head"><span class="t">General</span><?php echo afTog('enabled'); ?></div>
    <div class="body"><div class="grid">
      <div class="lab">Run when</div><div><select onChange="SetPluginSetting('pixelpulse','run_when',this.value,0,0);"><?php $rw = af_get('run_when', af_get('onlyWhenPlaying','1')==='1'?'playing':'always'); foreach (array('playing'=>'a sequence is playing','idle'=>'no sequence is playing','always'=>'always (whenever FPP outputs)') as $v=>$lbl) echo "<option value='$v'" . ($rw===$v?' selected':'') . ">$lbl</option>"; ?></select> <span class="help">test patterns are never touched</span></div>
      <div class="lab">Ambient mode</div><div><label class="sw"><input type="checkbox" id="af-ambient-cb"<?php echo af_chk('ambient_mode'); ?> onChange="afAmbient(this)"><span class="sl2"></span></label> <span class="help">loop designs when no show is scheduled; audio reacts on top</span></div>
      <div class="lab">Ambient playlist</div><div><select id="af-ambientpl" onChange="SetPluginSetting('pixelpulse','ambient_playlist',this.value,0,0);"><option value="">(blank — audio only)</option></select> <span class="help">your designs to loop; use a blend/brightness reaction so they stay visible</span></div>
    </div></div>
  </div>

  <div class="card">
    <div class="head"><span class="t">Physical switch (GPIO)</span><?php echo afTog('switch_enabled'); ?><span class="stat" style="margin-left:8px"><span class="dot" id="af-sw"></span><span id="af-swtxt" style="font-size:12.5px;color:#6b7280"></span></span></div>
    <div class="body"><div class="grid">
      <div class="lab">GPIO pin</div><div><select id="af-gpiopin" onChange="afGpioPick(this)"><option value="">&mdash; select a pin &mdash;</option></select> <span class="help">from FPP's pin list</span></div>
      <div class="lab">Pull resistor</div><div><select onChange="SetPluginSetting('pixelpulse','switch_pull',this.value,0,0);"><?php foreach (array('none'=>'none (external)','up'=>'pull-up (high)','down'=>'pull-down (low)') as $v=>$lbl) echo "<option value='$v'" . (af_get('switch_pull','none')===$v?' selected':'') . ">$lbl</option>"; ?></select></div>
      <div class="lab">Active level</div><div><select onChange="SetPluginSetting('pixelpulse','switch_active',this.value,0,0);"><?php foreach (array('high','low') as $m) echo "<option value='$m'" . (af_get('switch_active','high')===$m?' selected':'') . ">$m</option>"; ?></select> <span class="help">which level enables the plugin</span></div>
      <div class="lab"></div><div class="help">A hardware switch enables/disables the plugin (runs only when both the software toggle and the switch are on). Pin list and pull bias come from FPP's GPIO system. Read ~2x/sec.</div>
    </div></div>
  </div>

  <div class="card">
    <div class="head"><span class="t">Audio input</span></div>
    <div class="body"><div class="grid">
      <div class="lab">Device</div><div><select id="af-device" onChange="SetPluginSetting('pixelpulse','audioDevice',this.value,0,0);"><option value="<?php echo htmlspecialchars(af_get('audioDevice', 'default')); ?>"><?php echo htmlspecialchars(af_get('audioDevice', 'default')); ?> (current)</option></select> <span class="help">USB capture device</span></div>
      <div class="lab">Sample rate</div><div><select onChange="SetPluginSetting('pixelpulse','sampleRate',this.value,0,0);"><?php foreach (array('44100','48000') as $r) echo "<option value='$r'" . (af_get('sampleRate','44100')===$r?' selected':'') . ">$r</option>"; ?></select></div>
      <div class="lab">Input gain</div><div><?php echo afNum('gain', '1.0', '0.1', '10', '0.1'); ?></div>
      <div class="lab">Noise gate</div><div><?php echo afNum('gate', '0.02', '0', '0.3', '0.005'); ?></div>
      <div class="lab">Beat sensitivity</div><div><?php echo afNum('sensitivity', '1.5', '1.05', '3', '0.05'); ?></div>
      <div class="lab">Auto-calibrate</div><div><button type="button" id="af-calib" onclick="afCalibrate(this)" style="padding:7px 12px;border:1px solid #cdd3dc;border-radius:7px;background:#fff;cursor:pointer">Calibrate (listen 5s)</button> <span class="help">play typical audio, then it sets gain &amp; noise gate</span></div>
      <div class="lab">Calibrate silence</div><div><button type="button" id="af-calibsil" onclick="afCalibSilence(this)" style="padding:7px 12px;border:1px solid #cdd3dc;border-radius:7px;background:#fff;cursor:pointer">Calibrate silence (3s)</button> <span class="help">with NO music playing — captures &amp; removes the background noise</span></div>
    </div></div>
  </div>

  <div class="card">
    <div class="head"><span class="t">Audio tuning</span></div>
    <div class="body"><div class="grid">
      <div class="lab">Input channel</div><div><select onChange="SetPluginSetting('pixelpulse','input_channel',this.value,0,0);"><?php foreach (array('mix','left','right') as $m) echo "<option value='$m'" . (af_get('input_channel','mix')===$m?' selected':'') . ">$m</option>"; ?></select> <span class="help">use one side if the mic is wired to L or R</span></div>
      <div class="lab">Auto level (per song) <?php echo afTog('auto_level', '1'); ?></div><div class="help">Slowly auto-adjusts gain so quiet &amp; loud songs drive the lights equally.</div>
      <div class="lab">Auto-gain (AGC) <?php echo afTog('agc_enabled', '1'); ?></div><div class="help">Normalize loudness to the room. Off = absolute (set with Input gain).</div>
      <div class="lab">AGC speed</div><div><?php echo afNum('agc_speed', '0.5', '0', '1', '0.05'); ?></div>
      <div class="lab">Smoothing</div><div><?php echo afNum('smoothing', '0', '0', '0.95', '0.05'); ?> <span class="help">eases the fall so reactions breathe</span></div>
      <div class="lab">Noise reduction</div><div><?php echo afNum('noise_reduction', '1', '0', '1', '0.05'); ?> <span class="help">subtracts the captured background (Calibrate silence)</span></div>
      <div class="lab">Bass trim</div><div><?php echo afNum('bass_trim', '1', '0', '2', '0.05', '&times;'); ?></div>
      <div class="lab">Mid trim</div><div><?php echo afNum('mid_trim', '1', '0', '2', '0.05', '&times;'); ?></div>
      <div class="lab">Treble trim</div><div><?php echo afNum('treble_trim', '1', '0', '2', '0.05', '&times;'); ?></div>
    </div></div>
  </div>

  <div class="card">
    <div class="head"><span class="t">Range</span></div>
    <div class="body"><div class="grid">
      <div class="lab">Channels / pixel</div><div><select onChange="SetPluginSetting('pixelpulse','channelsPerPixel',this.value,0,0);"><?php foreach (array('3','4') as $c) echo "<option value='$c'" . (af_get('channelsPerPixel','3')===$c?' selected':'') . ">$c</option>"; ?></select></div>
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
      <div class="lab">Visualizer</div><div><select onChange="SetPluginSetting('pixelpulse','vis_mode',this.value,0,0);"><?php foreach (array('off','vu','spectrum') as $m) echo "<option value='$m'" . (af_get('vis_mode','off')===$m?' selected':'') . ">$m</option>"; ?></select> <span class="help">generates pixels from the audio over the range</span></div>
      <div class="lab">Spectral hue drive <?php echo afTog('hu_enabled', '0'); ?></div><div class="help">Shift the design's hue with bass/treble balance.</div>
      <div class="lab">Hue amount</div><div><?php echo afNum('hu_amount', '60', '0', '180', '5', 'deg'); ?></div>
    </div></div>
  </div>

  <div class="card">
    <div class="head"><span class="t">Audio → speed</span></div>
    <div class="body"><div class="grid">
      <div class="lab">Mode</div><div><select onChange="SetPluginSetting('pixelpulse','speed_mode',this.value,0,0);"><?php foreach (array('off','level','beat') as $m) echo "<option value='$m'" . (af_get('speed_mode','off')===$m?' selected':'') . ">$m</option>"; ?></select> <span class="help">light-only sequences</span></div>
      <div class="lab">Speed amount</div><div><?php echo afNum('speed_amount', '50', '0', '300', '5', '%'); ?></div>
    </div></div>
  </div>

  <div class="card">
    <div class="head"><span class="t">Spatial (xLights layout)</span><?php echo afTog('spatial_enabled'); ?></div>
    <div class="body"><div class="grid">
      <div class="lab">Layout file</div>
      <div>
        <input type="file" id="af-layout" accept=".xml">
        <button type="button" onclick="afUpload()" style="padding:7px 12px;border:1px solid #cdd3dc;border-radius:7px;background:#fff;cursor:pointer">Upload</button>
        <div class="help" id="af-layout-status" style="margin-top:6px">checking…</div>
      </div>
      <div class="lab">Mode</div><div><select id="af-spatialmode" onChange="SetPluginSetting('pixelpulse','spatial_mode',this.value,0,0);"><?php foreach (array('bloom','spectrum','vu','radial','pulse','spike','chase','sparkle','wave','fireworks','rain','strobe','colorwash','grow','spin','bars','ripple','fire','comet','plasma','scan','confetti','gravimeter','gravcenter','waterfall','djlight','puddles','fire2012','aurora','noise','twinkle','metaballs','bursts','drift','lissajous','tunnel','kaleido','vortex','rainbow','breathe','heartbeat','lightning','matrix','starburst','pinwheel','glitter','tide','radar','bounce','embers','mirror','dna','blocks','cylon','vuspiral','fireflies','strobepop','wipe','ribbons') as $m) echo "<option value='$m'" . (af_get('spatial_mode','bloom')===$m?' selected':'') . ">$m</option>"; ?></select> <span class="help">59 styles, driven by physical LED position</span></div>
      <div class="lab">Model group</div><div><select id="af-spatialgroup" onChange="SetPluginSetting('pixelpulse','spatial_group',this.value,0,0);"><option value="<?php echo htmlspecialchars(af_get('spatial_group','(all)')); ?>"><?php echo htmlspecialchars(af_get('spatial_group','(all)')); ?></option></select> <span class="help">limit to one xLights group</span></div>
      <div class="lab">With playback</div><div><select onChange="SetPluginSetting('pixelpulse','spatial_blend',this.value,0,0);"><?php foreach (array('replace'=>'replace (override)','overlay'=>'overlay (keep brighter)','add'=>'add (brighten)','modulate'=>'modulate (pulse the show)') as $v=>$lbl) echo "<option value='$v'" . (af_get('spatial_blend','replace')===$v?' selected':'') . ">$lbl</option>"; ?></select> <span class="help">override the sequence, or blend with it</span></div>
      <div class="lab">Auto design change</div><div><select onChange="afAutocycle(this);"><?php foreach (array('off','time','beats','smart') as $m) echo "<option value='$m'" . (af_get('spatial_autocycle','off')===$m?' selected':'') . ">$m</option>"; ?></select> <span class="help">smart = pick designs to match the music · when on, designs cycle on their own (no show needed)</span></div>
      <div class="lab">Change every</div><div><?php echo afNum('spatial_cyclesecs', '20', '3', '300', '1', 's'); ?></div>
      <div class="lab">Colour palette</div><div><select onChange="SetPluginSetting('pixelpulse','palette',this.value,0,0);"><?php foreach (array('auto','rainbow','fire','ocean','forest','sunset','aurora','party','lava','cloud','sherbet') as $m) echo "<option value='$m'" . (af_get('palette','auto')===$m?' selected':'') . ">$m</option>"; ?></select> <span class="help">themed colours (smart picks to match the music)</span></div>
      <div class="lab">Intensity</div><div><?php echo afNum('spatial_intensity', '100', '0', '200', '5', '%'); ?></div>
      <div class="lab">Fresh look each change</div><div><label class="sw"><input type="checkbox" id="af-fresh-cb"<?php echo af_chk('fresh_per_change','1'); ?> onChange="<?php echo af_js('fresh_per_change', true); ?>"><span class="sl2"></span></label> <span class="help">rotate palette (hue shift) + vary motion each design change</span></div>
      <div class="lab">Minimum glow</div><div><?php echo afNum('min_glow', '8', '0', '50', '1', '%'); ?> <span class="help">never goes dark — keeps a floor of LEDs lit</span></div>
      <div class="lab"></div><div class="help">Upload your <b>xlights_rgbeffects.xml</b>. When enabled, this renders the whole display from the audio by each prop's real position — overriding the Range pipeline above.</div>
    </div></div>
  </div>
</div>

<script>
function afxToggle(cb){}
function afApi(p){ return 'plugin.php?plugin=pixelpulse&page=' + p + '&nopage=1'; }
// physical-switch GPIO pin dropdown, populated from FPP's pin list
function afGpioPick(sel){ var o=sel.options[sel.selectedIndex]; if(!o||!o.value) return; var pp=o.value.split(':');
  SetPluginSetting('pixelpulse','switch_gpio',pp[0],0,0);
  SetPluginSetting('pixelpulse','switch_chip',pp[1],0,0);
  SetPluginSetting('pixelpulse','switch_line',pp[2],0,0); }
// Save & Restart: settings already persist on change; this restarts fppd so
// everything (incl. audio-device/capture changes made while idle) applies cleanly.
function afSaveRestart(btn){
  if(!confirm('Restart the fppd daemon now to apply all Pixel Pulse settings?\nThe lights pause for a few seconds. This is not a reboot.')) return;
  var stat=document.getElementById('af-saverestart-stat'), orig=btn.textContent;
  btn.disabled=true; btn.textContent='Restarting…'; stat.textContent='restarting fppd…';
  fetch('api/system/fppd/restart').catch(function(){});
  setTimeout(function(){
    var tries=0, iv=setInterval(function(){
      tries++;
      fetch('api/fppd/version',{cache:'no-store'}).then(function(r){return r.ok?r.text():Promise.reject();}).then(function(t){
        if(!t) return Promise.reject();
        clearInterval(iv); btn.disabled=false; btn.textContent=orig; stat.textContent='✓ applied'; setTimeout(function(){stat.textContent='';},5000);
      }).catch(function(){ if(tries>25){ clearInterval(iv); btn.disabled=false; btn.textContent=orig; stat.textContent='still restarting — give it a moment'; } });
    }, 1500);
  }, 4000);
}
// auto design change: when turned on, the plugin self-runs the output loop so
// designs cycle with no show — make sure that loop's files exist
function afAutocycle(sel){
  SetPluginSetting('pixelpulse','spatial_autocycle',sel.value,0,0);
  if(sel.value!=='off') fetch(afApi('ambient.php')+'&setup=1').catch(function(){});
}
// ambient mode: create the blank sequence/playlist on enable; the plugin keeps it looping when idle
function afAmbient(cb){
  SetPluginSetting('pixelpulse','ambient_mode',cb.checked?1:0,0,0);
  if(cb.checked){
    fetch(afApi('ambient.php')+'&setup=1').then(function(r){return r.json();}).then(function(d){
      if(!d||!d.ok) alert('Ambient setup failed (could not write the blank sequence/playlist).');
    }).catch(function(){});
  } else {
    fetch('api/fppd/status').then(function(r){return r.json();}).then(function(s){   // stop only our ambient loop
      var pl=s.current_playlist, name=(pl&&pl.playlist)||pl||'';
      var chosen='<?php echo htmlspecialchars(af_get("ambient_playlist","")); ?>';
      if(name==='pixelpulse_ambient' || (chosen && name===chosen)) fetch('api/command',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({command:'Stop Now'})});
    }).catch(function(){});
  }
}
fetch('api/playlists').then(function(r){return r.json();}).then(function(list){   // populate ambient playlist choices
  var sel=document.getElementById('af-ambientpl'); if(!sel) return;
  var cur='<?php echo htmlspecialchars(af_get("ambient_playlist","")); ?>';
  (list||[]).forEach(function(pl){ if(pl==='pixelpulse_ambient') return; var o=document.createElement('option'); o.value=pl; o.textContent=pl; if(pl===cur) o.selected=true; sel.appendChild(o); });
}).catch(function(){});
fetch('api/gpio').then(function(r){return r.json();}).then(function(list){
  var sel=document.getElementById('af-gpiopin'); if(!sel) return;
  var cur='<?php echo htmlspecialchars(af_get("switch_gpio","-1")); ?>';
  (list||[]).forEach(function(g){ var o=document.createElement('option');
    o.value=g.gpio+':'+g.gpioChip+':'+g.gpioLine;
    o.textContent=g.pin+'  (gpio '+g.gpio+((g.supportsPullUp||g.supportsPullDown)?', pull-able':'')+')';
    if((''+g.gpio)===cur) o.selected=true; sel.appendChild(o); });
}).catch(function(){});
// spatial layout upload + status
function afLayoutStatus(s){
  var el=document.getElementById('af-layout-status'); if(!el) return;
  if(s && s.count>0){ el.textContent='layout loaded: '+s.count+' LEDs'+(s.groups&&s.groups.length?(' · '+s.groups.length+' groups'):''); el.style.color='#2f9e6f'; }
  else { el.textContent='no layout uploaded — choose your xlights_rgbeffects.xml'; el.style.color='#6b7280'; }
}
function afRefreshLayout(){ fetch(afApi('uploadlayout.php')).then(function(r){return r.json();}).then(afLayoutStatus).catch(function(){}); }
function afUpload(){
  var inp=document.getElementById('af-layout'), f=inp&&inp.files[0];
  if(!f){ alert('Choose your xlights_rgbeffects.xml first'); return; }
  var el=document.getElementById('af-layout-status'); el.textContent='uploading & parsing…'; el.style.color='#6b7280';
  var fd=new FormData(); fd.append('layout',f);
  fetch(afApi('uploadlayout.php'),{method:'POST',body:fd}).then(function(r){return r.json();}).then(function(s){
    if(s.ok){ afLayoutStatus(s); } else { el.textContent='error: '+(s.error||'parse failed'); el.style.color='#e24b4a'; }
  }).catch(function(){ el.textContent='upload failed'; el.style.color='#e24b4a'; });
}
afRefreshLayout();
// silence calibration: capture the background-noise spectrum + set the gate above it
function afCalibSilence(btn){
  SetPluginSetting('pixelpulse','noise_learn',String(Date.now()),0,0);  // trigger the plugin's spectral learn
  var samples=[], n=0, total=35, orig=btn.textContent; btn.disabled=true;
  var iv=setInterval(function(){
    var s=window.afLast; if(s&&typeof s.rawLevel==='number') samples.push(s.rawLevel);
    n++; btn.textContent='listening (keep silent)… '+Math.max(0,Math.ceil((total-n)/10))+'s';
    if(n<total) return;
    clearInterval(iv); btn.disabled=false; btn.textContent=orig;
    if(samples.length<5){ alert('No audio captured — make sure the device is open.'); return; }
    var floor=afPctl(samples,0.9);
    var gate=Math.min(0.3,Math.max(0.004, floor*1.3+0.004)); gate=Math.round(gate/0.005)*0.005; gate=Math.round(gate*1000)/1000;
    SetPluginSetting('pixelpulse','gate',gate,0,0); afSetSlider('gate',gate);
    alert('Silence calibrated:\n  background spectrum captured (noise reduction on)\n  noise gate set to '+gate);
  },100);
}
// one-click auto-calibrate: listen ~5s, set noise gate from the floor and gain from the peaks
function afSetSlider(k,v){ var i=document.getElementById('afn-'+k); if(i) i.value=v; var sp=document.getElementById('afnv-'+k); if(sp) sp.textContent=v+(sp.getAttribute('data-u')||''); }
function afPctl(arr,p){ if(!arr.length) return 0; var a=arr.slice().sort(function(x,y){return x-y;}); return a[Math.min(a.length-1,Math.floor(p*a.length))]; }
function afCalibrate(btn){
  var samples=[], n=0, total=50, orig=btn.textContent; btn.disabled=true;
  var iv=setInterval(function(){
    var s=window.afLast; if(s&&typeof s.rawLevel==='number') samples.push(s.rawLevel);
    n++; btn.textContent='listening… '+Math.max(0,Math.ceil((total-n)/10))+'s';
    if(n<total) return;
    clearInterval(iv); btn.disabled=false; btn.textContent=orig;
    var act=samples.filter(function(v){return v>0;});
    if(act.length<5){ alert('No audio captured — make sure the device is open and play some sound, then calibrate.'); return; }
    var floor=afPctl(samples,0.25), peak=afPctl(samples,0.95);
    var gate=Math.min(0.2,Math.max(0.005, floor*1.8+0.003)); gate=Math.round(gate/0.005)*0.005; gate=Math.round(gate*1000)/1000;
    var gain=Math.min(10,Math.max(0.3, 0.35/Math.max(0.01,peak))); gain=Math.round(gain*10)/10;
    SetPluginSetting('pixelpulse','gate',gate,0,0); afSetSlider('gate',gate);
    SetPluginSetting('pixelpulse','gain',gain,0,0); afSetSlider('gain',gain);
    alert('Calibrated from '+act.length+' samples:\n  gain = '+gain+'\n  noise gate = '+gate);
  },100);
}
// live spatial preview: the layout lit by the on-device audio, current mode
var afPts=null, afGroups=[], afCanvasLive=document.getElementById('af-preview-live'), afCanvasFx=document.getElementById('af-preview-fx');
function afHsv(h,s,v){ h=((h%360)+360)%360; var c=v*s,x=c*(1-Math.abs((h/60)%2-1)),m=v-c,r=0,g=0,b=0;
  if(h<60){r=c;g=x;}else if(h<120){r=x;g=c;}else if(h<180){g=c;b=x;}else if(h<240){g=x;b=c;}else if(h<300){r=x;b=c;}else{r=c;b=x;}
  return 'rgb('+Math.round((r+m)*255)+','+Math.round((g+m)*255)+','+Math.round((b+m)*255)+')'; }
var afPals={
  rainbow:[[0,255,0,0],[.17,255,255,0],[.33,0,255,0],[.5,0,255,255],[.66,0,0,255],[.83,255,0,255],[1,255,0,0]],
  fire:[[0,0,0,0],[.3,200,0,0],[.6,255,110,0],[.85,255,230,0],[1,255,255,200]],
  ocean:[[0,0,0,50],[.3,0,40,130],[.6,0,120,190],[.85,0,210,200],[1,170,255,255]],
  forest:[[0,0,40,0],[.3,0,110,0],[.6,70,170,0],[.85,170,210,0],[1,220,255,150]],
  sunset:[[0,40,0,70],[.3,170,0,80],[.55,255,80,0],[.8,255,180,0],[1,255,240,130]],
  aurora:[[0,0,40,30],[.3,0,170,120],[.55,90,225,160],[.8,150,120,225],[1,220,160,255]],
  party:[[0,255,0,130],[.2,255,0,0],[.4,255,160,0],[.6,0,200,90],[.8,0,120,255],[1,180,0,255]],
  lava:[[0,0,0,0],[.3,120,0,0],[.6,225,45,0],[.85,255,140,0],[1,255,225,90]],
  cloud:[[0,0,0,90],[.3,0,40,170],[.6,90,90,225],[.85,160,185,255],[1,255,255,255]],
  sherbet:[[0,255,80,160],[.3,255,160,120],[.55,255,255,130],[.8,160,255,200],[1,200,200,255]] };
function afPalCol(name,t,br){ var P=afPals[name]; if(!P) return null; t-=Math.floor(t);
  var i=0; while(i<P.length-1 && t>P[i+1][0]) i++;
  var a=P[i], b=P[i<P.length-1?i+1:i], span=b[0]-a[0], f=span>1e-4?(t-a[0])/span:0; if(f<0)f=0; if(f>1)f=1;
  return 'rgb('+Math.round((a[1]+(b[1]-a[1])*f)*br)+','+Math.round((a[2]+(b[2]-a[2])*f)*br)+','+Math.round((a[3]+(b[3]-a[3])*f)*br)+')'; }
var afSt={t:performance.now(),latch:false,ring:false,ringPh:0,chase:0,wave:0,spin:0,ripple:0,comet:0,scan:0,rain:[-1,-1,-1],bursts:[],vu:0,vuPeak:0,wf:[],wfAccum:0,puddles:[],heat:new Array(32).fill(0),fireAccum:0,ballX:[.5,.5,.5],ballY:[.5,.5,.5],ballR:.02,lissX:new Array(8).fill(.5),lissY:new Array(8).fill(.5),heart:0,matrix:0,beatCount:0,hueShift:0,varSpeed:1,varDir:1,lastMode:''};
function afPrevLoop(){
  requestAnimationFrame(afPrevLoop);
  if(!afPts) return;
  var now=performance.now(), dt=Math.min(0.2,(now-afSt.t)/1000); afSt.t=now;
  var s=window.afLast||{level:0,beat:0,bass:0,treble:0,bands:[]};
  var mode=s.spatialMode||((document.getElementById('af-spatialmode')||{}).value)||'bloom';
  var frame=window.afFrame, haveLive=(frame && frame.length>=afPts.length-2);
  document.getElementById('af-prevmode').textContent=mode+(s.musicType?(' · '+s.musicType):'');
  var ls=document.getElementById('af-live-stat'); if(ls) ls.textContent=haveLive?'':'(play a sequence)';
  var bands=s.bands||[], nb=bands.length||8, lvl=s.level||0, beat=s.beat||0, bass=s.bass||0, treble=s.treble||0;
  var gsel=document.getElementById('af-spatialgroup'), gval=gsel?gsel.value:'(all)';
  var gidx=afGroups.indexOf(gval), gfilter=(gval!=='(all)'&&gidx>=0), gbit=gidx>=0?(1<<gidx):0;
  // advance shared effect state once per frame
  var trig=false; if(beat>0.5&&!afSt.latch){afSt.latch=true;trig=true;afSt.beatCount++;} if(beat<0.2)afSt.latch=false;
  // fresh look per design change: rotate palette + vary motion (mirror of the engine)
  var afFresh=!(document.getElementById('af-fresh-cb')) || document.getElementById('af-fresh-cb').checked;
  var afMinG=(parseFloat((document.getElementById('afn-min_glow')||{}).value)||0)/100;
  if(mode!==afSt.lastMode){ afSt.lastMode=mode; if(afFresh){ afSt.hueShift=(afSt.hueShift+80+Math.random()*90)%360; afSt.varSpeed=0.7+Math.random()*0.8; afSt.varDir=(Math.random()<0.5)?1:-1; } }
  var vs=afFresh?afSt.varSpeed:1, vd=afFresh?afSt.varDir:1;
  if(afFresh){ afSt.hueShift=(afSt.hueShift+dt*5)%360; }
  afSt.chase+=dt*(0.12+0.5*lvl)*vs; afSt.wave+=dt*0.6*vs;
  afSt.spin+=dt*(0.08+0.25*lvl)*vs*vd; afSt.spin-=Math.floor(afSt.spin);
  afSt.ripple+=dt*(0.25+0.6*lvl)*vs*vd; afSt.ripple-=Math.floor(afSt.ripple);
  afSt.comet+=dt*(0.22+0.5*lvl)*vs*vd; afSt.comet-=Math.floor(afSt.comet);
  afSt.scan+=dt*(0.25+0.6*lvl)*vs*vd; afSt.scan-=Math.floor(afSt.scan);
  var _bpm=s.bpm||0; afSt.heart+=dt*(_bpm>30?_bpm/60:1.25); afSt.heart-=Math.floor(afSt.heart);
  afSt.matrix+=dt*(0.3+0.7*lvl); afSt.matrix-=Math.floor(afSt.matrix);
  if(trig){afSt.ring=true;afSt.ringPh=0;} if(afSt.ring){afSt.ringPh+=dt/0.6; if(afSt.ringPh>1.5)afSt.ring=false;}
  if(trig&&afSt.bursts.length<5){ var q=afPts[Math.floor(Math.random()*afPts.length)]; afSt.bursts.push({x:q[0],y:q[1],age:0}); }
  afSt.bursts.forEach(function(b){b.age+=dt;}); afSt.bursts=afSt.bursts.filter(function(b){return b.age<=1.2;});
  if(trig){ for(var r=0;r<afSt.rain.length;r++) if(afSt.rain[r]<0){afSt.rain[r]=1.05;break;} }
  for(var r2=0;r2<afSt.rain.length;r2++) if(afSt.rain[r2]>=0){afSt.rain[r2]-=dt/1.1; if(afSt.rain[r2]<-0.1)afSt.rain[r2]=-1;}
  if(lvl>afSt.vu)afSt.vu=lvl; else afSt.vu-=1.2*dt; if(afSt.vu<0)afSt.vu=0;
  if(afSt.vu>afSt.vuPeak)afSt.vuPeak=afSt.vu; else afSt.vuPeak-=0.35*dt; if(afSt.vuPeak<0)afSt.vuPeak=0;
  afSt.wfAccum+=dt; if(afSt.wfAccum>=0.04){ afSt.wfAccum-=0.04; afSt.wf.push(bands.slice(0,nb)); if(afSt.wf.length>64)afSt.wf.shift(); }
  if(trig&&afSt.puddles.length<4){ var pq=afPts[Math.floor(Math.random()*afPts.length)]; afSt.puddles.push({x:pq[0],y:pq[1],age:0}); }
  afSt.puddles.forEach(function(pu){pu.age+=dt;}); afSt.puddles=afSt.puddles.filter(function(pu){return pu.age<=1.4;});
  afSt.fireAccum+=dt; if(afSt.fireAccum>=0.03){ afSt.fireAccum-=0.03; var HN=afSt.heat.length;
    for(var hk=0;hk<HN;hk++){ afSt.heat[hk]-=Math.random()*(0.5/HN+0.015); if(afSt.heat[hk]<0)afSt.heat[hk]=0; }
    for(var hk2=HN-1;hk2>=2;hk2--) afSt.heat[hk2]=(afSt.heat[hk2-1]+afSt.heat[hk2-2]+afSt.heat[hk2-2])/3;
    if(Math.random()<0.5+0.5*bass){ var hy=Math.floor(Math.random()*(HN/4+1)); afSt.heat[hy]+=0.5+0.5*Math.random()*(0.5+0.5*bass); if(afSt.heat[hy]>1)afSt.heat[hy]=1; } }
  var bt=afSt.wave;
  afSt.ballX[0]=0.5+0.40*Math.sin(bt*0.7); afSt.ballY[0]=0.5+0.40*Math.cos(bt*0.9);
  afSt.ballX[1]=0.5+0.35*Math.sin(bt*1.1+2); afSt.ballY[1]=0.5+0.40*Math.cos(bt*0.6+1);
  afSt.ballX[2]=0.5+0.40*Math.sin(bt*0.5+4); afSt.ballY[2]=0.5+0.35*Math.cos(bt*1.3+3); afSt.ballR=0.012+0.03*lvl;
  for(var lk=7;lk>0;lk--){afSt.lissX[lk]=afSt.lissX[lk-1];afSt.lissY[lk]=afSt.lissY[lk-1];}
  var lt=afSt.wave*1.5; afSt.lissX[0]=0.5+0.42*Math.sin(lt*3+1); afSt.lissY[0]=0.5+0.42*Math.sin(lt*2);
  var dom=0,dmax=0; for(var b3=0;b3<nb;b3++){ if((bands[b3]||0)>dmax){dmax=bands[b3];dom=b3;} }
  var chase=afSt.chase%1;
  var pal=(s.palette&&s.palette!=='auto'&&afPals[s.palette])?s.palette:null;
  function fxColor(i,nx,ny,dist){
    var br=0,hue=0,sat=1,bi,dd,tw,wv;
    switch(mode){
      case 'bloom': if(afSt.ring) br=Math.exp(-Math.pow((dist-afSt.ringPh)/0.16,2)); br*=(0.45+0.55*lvl); hue=210-170*bass; break;
      case 'spectrum': bi=Math.min(nb-1,Math.floor(nx*nb)); br=bands[bi]||0; hue=280*nx; break;
      case 'vu': br=(ny<=lvl)?(0.4+0.6*(1-(lvl-ny))):0; hue=120*(1-ny); break;
      case 'radial': bi=Math.min(nb-1,Math.floor(dist*nb)); br=bands[bi]||0; hue=200+100*dist; break;
      case 'pulse': br=0.1+0.9*lvl; hue=210-170*bass+90*treble; break;
      case 'spike': br=beat; hue=40+200*bass; break;
      case 'chase': dd=Math.abs(nx-chase); dd=Math.min(dd,1-dd); br=Math.exp(-Math.pow(dd/0.10,2))*(0.4+0.6*lvl); hue=200+120*nx; break;
      case 'sparkle': tw=Math.sin(afSt.wave*6+nx*53+ny*97); br=(tw>(1-0.5*lvl-(trig?0.4:0)))?1:0; hue=180+120*ny; break;
      case 'wave': wv=0.5+0.5*Math.sin((nx+ny)*9.42-afSt.wave*6.2832); br=wv*(0.25+0.75*lvl); hue=260*wv; break;
      case 'fireworks': afSt.bursts.forEach(function(bb){ var rd=Math.hypot(nx-bb.x,ny-bb.y); br+=Math.exp(-Math.pow((rd-bb.age*0.9)/0.08,2))*(1-bb.age/1.2); }); br=Math.min(1,br)*(0.5+0.5*lvl); hue=30+300*bass; break;
      case 'rain': afSt.rain.forEach(function(rf){ if(rf>=0) br+=Math.exp(-Math.pow((ny-rf)/0.10,2)); }); br=Math.min(1,br); hue=200; break;
      case 'strobe': br=(beat>0.5)?1:0; sat=0; break;
      case 'colorwash': br=0.15+0.85*lvl; hue=280*dom/Math.max(1,nb-1); break;
      case 'grow': br=(dist<=lvl*1.15)?(0.5+0.5*lvl):0; hue=140-120*dist; break;
      case 'spin': var ang=Math.atan2(ny-0.5,nx-0.5)*57.2958; br=0.2+0.8*lvl; hue=ang+afSt.spin*360+180*dist; break;
      case 'bars': var col=Math.min(nb-1,Math.floor(nx*nb)), hh=bands[col]||0; br=(ny<=hh)?(0.4+0.6*hh):0; hue=280*nx; break;
      case 'ripple': var rv=0.5+0.5*Math.sin((dist*5-afSt.ripple)*6.2832); br=Math.pow(rv,3)*(0.3+0.7*lvl); hue=190+130*dist; break;
      case 'fire': var fl=0.55+0.45*Math.sin(afSt.wave*8+nx*40+ny*25), fb=(1-ny)*(1-ny); br=fb*(0.35+0.65*bass)*fl*(0.5+0.5*lvl); hue=50*Math.min(1,br*1.3); break;
      case 'comet': var cd=afSt.comet-nx; if(cd<0)cd+=1; br=(cd<0.35)?(1-cd/0.35)*(0.4+0.6*lvl):0; hue=190+90*cd; break;
      case 'plasma': var pv=Math.sin(nx*6+afSt.wave*3)+Math.sin(ny*6+afSt.wave*2)+Math.sin((nx+ny)*5+afSt.wave); pv=(pv/3+1)*0.5; br=(0.3+0.7*lvl)*(0.4+0.6*pv); hue=360*pv; break;
      case 'scan': var sc=0.5+0.5*Math.sin(afSt.scan*6.2832), sd=Math.abs(ny-sc); br=Math.exp(-Math.pow(sd/0.07,2))*(0.4+0.6*lvl); hue=30*sc; break;
      case 'confetti': var c1=Math.sin(i*12.9898)*43758.5453; c1-=Math.floor(c1); br=(c1<0.15+0.35*beat)?beat:0; var c2=Math.sin(i*78.233)*43758.5453; c2-=Math.floor(c2); hue=360*c2; break;
      case 'gravimeter': br=(ny<=afSt.vu)?(0.4+0.6*(1-(afSt.vu-ny))):0; if(Math.abs(ny-afSt.vuPeak)<0.02)br=1; hue=360*ny; break;
      case 'gravcenter': var dc=Math.abs(ny-0.5)*2; br=(dc<=afSt.vu)?(0.4+0.6*(1-(afSt.vu-dc))):0; if(Math.abs(dc-afSt.vuPeak)<0.03)br=1; hue=360*dc; break;
      case 'waterfall': if(afSt.wf.length){ var ti=Math.min(afSt.wf.length-1,Math.floor(ny*afSt.wf.length)); var row=afSt.wf[afSt.wf.length-1-ti]; if(row){ var wbi=Math.min(nb-1,Math.floor(nx*nb)); br=row[wbi]||0; } } hue=280*nx; break;
      case 'djlight': var e; if(dist<0.34){e=bass;hue=0;}else if(dist<0.67){e=s.mid||0;hue=120;}else{e=treble;hue=240;} br=0.12+0.88*e; break;
      case 'puddles': afSt.puddles.forEach(function(pu){ var rd=Math.hypot(nx-pu.x,ny-pu.y), radius=pu.age*0.3; if(radius>0&&rd<radius) br+=(1-rd/radius)*(1-pu.age/1.4); }); br=Math.min(1,br); hue=360*nx; break;
      case 'fire2012': var hi=Math.min(afSt.heat.length-1,Math.floor(ny*(afSt.heat.length-1))); var hh2=afSt.heat[hi]||0; br=hh2; hue=360*hh2; break;
      case 'aurora': var av=0.5+0.3*Math.sin(ny*4+afSt.wave*0.8)+0.2*Math.sin(nx*3-afSt.wave*0.5)+0.2*Math.sin((nx+ny)*2+afSt.wave*0.3); if(av<0)av=0; if(av>1)av=1; br=(0.2+0.6*av)*(0.55+0.45*lvl); hue=360*av; break;
      case 'noise': var nv=Math.sin(nx*8+afSt.wave*2)*Math.cos(ny*7-afSt.wave*1.5)+Math.sin((nx*ny)*12+afSt.wave); nv=(nv+2)*0.25; br=(0.25+0.75*lvl)*(0.4+0.6*nv); hue=360*nv; break;
      case 'twinkle': var tw2=Math.sin(afSt.wave*1.8+i*2.399); var on2=tw2>0.5?(tw2-0.5)*2:0; br=on2*(0.5+0.5*lvl); var th=Math.sin(i*0.0173)*43758.5453; th-=Math.floor(th); hue=360*th; break;
      case 'metaballs': var mf=0; for(var mk=0;mk<3;mk++){ var ax=nx-afSt.ballX[mk], ay=ny-afSt.ballY[mk]; mf+=afSt.ballR/(ax*ax+ay*ay+0.004); } br=mf>1?1:(mf<0.25?0:(mf-0.25)/0.75); hue=360*nx; break;
      case 'bursts': var an=Math.atan2(ny-0.5,nx-0.5); var bv=0.5+0.5*Math.sin(an*6+afSt.spin*6.2832+dist*8); br=bv*bv*(0.3+0.7*lvl); hue=360*(an/6.2832+0.5); break;
      case 'drift': var an2=Math.atan2(ny-0.5,nx-0.5); var dv=0.5+0.5*Math.sin(an2*3+dist*10-afSt.spin*6.2832); br=dv*dv*(0.3+0.7*lvl); hue=360*dist; break;
      case 'tunnel': var tz=dist*3-afSt.ripple*2; tz-=Math.floor(tz); br=Math.pow(0.5+0.5*Math.sin(tz*6.2832),2)*(0.3+0.7*lvl); hue=(360*tz+afSt.spin*360)%360; break;
      case 'kaleido': var ka=Math.atan2(ny-0.5,nx-0.5)+3.14159, kseg=1.0472, kw=Math.abs((ka%kseg)-kseg*0.5); var kv=0.5+0.5*Math.sin(kw*12+afSt.wave*3-dist*10); br=kv*kv*(0.35+0.65*lvl); hue=(360*(kw/(kseg*0.5))+afSt.spin*360)%360; break;
      case 'vortex': var va=Math.atan2(ny-0.5,nx-0.5); var vv=0.5+0.5*Math.sin(va*2+Math.log(dist+0.05)*6-afSt.spin*12.566); br=vv*vv*(0.3+0.7*lvl); hue=(360*(va/6.2832+0.5)+afSt.spin*180)%360; break;
      case 'rainbow': var rs=nx+ny*0.25-afSt.comet; rs-=Math.floor(rs); br=0.45+0.55*lvl; hue=360*rs; break;
      case 'breathe': var bb2=0.5+0.5*Math.sin(afSt.wave*1.2); br=(0.2+0.8*lvl)*(0.45+0.55*bb2)*(1-0.35*dist); hue=(afSt.wave*18)%360; break;
      case 'heartbeat': var hph=afSt.heart, ht=Math.exp(-Math.pow(hph/0.05,2))+0.6*Math.exp(-Math.pow((hph-0.16)/0.05,2)); if(ht>1)ht=1; br=ht*(0.4+0.6*lvl)*(1-0.25*dist); hue=350; sat=1-0.5*ht; break;
      case 'lightning': var lbx=0.5+0.42*Math.sin(afSt.scan*8.168), lon=(treble>0.35)?(0.4+0.6*treble):0, ldd=Math.abs(nx-lbx); br=Math.exp(-Math.pow(ldd/0.035,2))*lon; hue=215; sat=0.45; break;
      case 'matrix': var mcol=Math.min(23,Math.floor(nx*24)); var mh=Math.sin(mcol*12.9898)*43758.5453; mh-=Math.floor(mh); var mph=(afSt.matrix*(0.5+mh)+mh)%1; var mhead=1.1-mph*1.2, md=ny-mhead; br=(md>=0&&md<0.4)?(1-md/0.4)*(0.4+0.6*lvl):0; hue=130; break;
      case 'starburst': if(afSt.ring){ var sba=Math.atan2(ny-0.5,nx-0.5); var spk=Math.pow(0.5+0.5*Math.sin(sba*8),3); br=spk*Math.exp(-Math.pow((dist-afSt.ringPh*0.8)/0.18,2)); } br*=(0.5+0.5*lvl); hue=40+280*bass; break;
      case 'pinwheel': var pa=Math.atan2(ny-0.5,nx-0.5)/6.2832+0.5, ps=pa+afSt.spin; ps-=Math.floor(ps); var psect=Math.floor(ps*8); br=(0.3+0.7*lvl)*(0.6+0.4*Math.cos(dist*6)); hue=psect*45; break;
      case 'glitter': var gh=Math.sin(i*7.13+Math.floor(afSt.wave*12)*3.7)*43758.5453; gh-=Math.floor(gh); br=(gh>0.97-0.4*treble)?1:0; br*=(0.5+0.5*lvl); sat=0.15; hue=210; break;
      case 'tide': var tsurf=0.12+0.85*afSt.vu+0.05*Math.sin(nx*9+afSt.wave*4); br=(ny<=tsurf)?(0.35+0.65*(1-(tsurf-ny))):0; hue=200-60*ny; break;
      case 'radar': var ra=Math.atan2(ny-0.5,nx-0.5)/6.2832+0.5, rsw=ra-afSt.spin; rsw-=Math.floor(rsw); br=(rsw<0.3)?(1-rsw/0.3):0; br*=(0.3+0.7*lvl)*(0.35+0.65*dist); hue=120; break;
      case 'bounce': var bby=0.08+0.84*Math.abs(Math.sin(afSt.scan*3.14159)), bdd=Math.abs(ny-bby); br=Math.exp(-Math.pow(bdd/0.09,2))*(0.4+0.6*lvl); hue=20+320*bby; break;
      case 'embers': var eh=Math.sin(i*2.71)*43758.5453; eh-=Math.floor(eh); var ey=(eh+afSt.wave*0.4)%1, edd=Math.abs(ny-ey); br=Math.exp(-Math.pow(edd/0.05,2))*(0.4+0.6*bass)*(0.5+0.5*lvl)*(1-0.6*ey); hue=32-32*ey; break;
      case 'mirror': var mbi=Math.min(nb-1,Math.floor(Math.abs(nx-0.5)*2*nb)), mhh=bands[mbi]||0; br=(Math.abs(ny-0.5)*2<=mhh)?(0.4+0.6*mhh):0; hue=280*Math.abs(nx-0.5)*2; break;
      case 'dna': var da=nx*9+afSt.wave*3, ds1=0.5+0.4*Math.sin(da), ds2=0.5-0.4*Math.sin(da), dd1=Math.abs(ny-ds1), dd2=Math.abs(ny-ds2); br=(Math.exp(-Math.pow(dd1/0.06,2))+Math.exp(-Math.pow(dd2/0.06,2)))*(0.4+0.6*lvl); hue=(dd1<dd2)?200:320; break;
      case 'blocks': var bcx=Math.floor(nx*6), bcy=Math.floor(ny*6), bon=((bcx+bcy+afSt.beatCount)&1), bbi=(bcx+bcy)%Math.max(1,nb); br=bon?(0.2+0.8*(bands[bbi]||0)):0; hue=60*((bcx+bcy)%6); break;
      case 'cylon': var cye=0.5+0.46*Math.sin(afSt.scan*6.2832), cdd=Math.abs(nx-cye); br=Math.exp(-Math.pow(cdd/0.07,2))*(0.4+0.6*lvl); hue=0; break;
      case 'vuspiral': var vsa=Math.atan2(ny-0.5,nx-0.5)/6.2832+0.5, vsp=vsa*0.25+dist; vsp-=Math.floor(vsp); br=(vsp<=afSt.vu)?(0.4+0.6*(1-(afSt.vu-vsp))):0; hue=360*vsp; break;
      case 'fireflies': var fh=Math.sin(i*4.19)*43758.5453; fh-=Math.floor(fh); var ftw=Math.sin(afSt.wave*0.9+fh*31.4); br=(ftw>0.75)?(ftw-0.75)*4:0; br*=(0.5+0.5*lvl); var fh2=Math.sin(i*0.07)*43758.5453; fh2-=Math.floor(fh2); hue=80+60*fh2; break;
      case 'strobepop': br=(beat>0.5)?1:0; hue=((afSt.beatCount*0.27)%1)*360; break;
      case 'wipe': var ww=afSt.comet, wdd=nx-ww; if(wdd<0)wdd+=1; br=Math.pow(1-wdd,2)*(0.35+0.65*lvl); hue=(afSt.comet*360+110*ny)%360; break;
      case 'ribbons': var rr=0; for(var rk=0;rk<3;rk++){ var ryc=0.25+0.25*rk+0.12*Math.sin(nx*6+afSt.wave*(2+rk)+rk); var re=Math.exp(-Math.pow((ny-ryc)/0.05,2)); if(re>rr)rr=re; } br=rr*(0.35+0.65*lvl); hue=(afSt.wave*30+120*ny)%360; break;
      default: for(var lk2=0;lk2<8;lk2++){ var lax=nx-afSt.lissX[lk2], lay=ny-afSt.lissY[lk2], ld=lax*lax+lay*lay, la=(8-lk2)/8; var lb=Math.exp(-ld/0.008)*la; if(lb>br)br=lb; } br*=(0.5+0.5*lvl); hue=360*((afSt.wave*0.2)%1); break;
    }
    if(afFresh) hue+=afSt.hueShift;
    if(br<afMinG) br=afMinG;
    if(br<0)br=0; if(br>1)br=1;
    var hh=((hue%360)+360)%360;
    var col = pal ? afPalCol(pal, hh/360, Math.max(0.05,br)) : afHsv(hh,sat,Math.max(0.05,br));
    return {br:br, col:col};
  }
  function renderOne(cv, useLive){
    if(!cv) return;
    var dpr=Math.min(2,window.devicePixelRatio||1), W=cv.clientWidth, H=cv.clientHeight; if(!W) return;
    if(cv.width!==Math.round(W*dpr)){ cv.width=Math.round(W*dpr); cv.height=Math.round(H*dpr); }
    var ctx=cv.getContext('2d'); ctx.setTransform(dpr,0,0,dpr,0,0); ctx.clearRect(0,0,W,H);
    var pad=8, sw=W-2*pad, sh=H-2*pad, i, p, nx, ny;
    ctx.fillStyle='rgba(150,162,178,0.26)';
    for(i=0;i<afPts.length;i++){ ctx.beginPath(); ctx.arc(pad+afPts[i][0]*sw, pad+(1-afPts[i][1])*sh, 1.1, 0, 6.283); ctx.fill(); }
    for(i=0;i<afPts.length;i++){
      p=afPts[i]; nx=p[0]; ny=p[1];
      if(useLive){
        var fc=frame&&frame[i]; if(!fc) continue;
        var lum=Math.min(1,(fc[0]+fc[1]+fc[2])/600); if(lum<=0.015) continue;
        ctx.globalAlpha=0.2+0.8*lum; ctx.fillStyle='rgb('+fc[0]+','+fc[1]+','+fc[2]+')';
        ctx.beginPath(); ctx.arc(pad+nx*sw, pad+(1-ny)*sh, 1.4+2.6*lum, 0, 6.283); ctx.fill();
      } else {
        if(gfilter && !((p[3]||0)&gbit)) continue;
        var c=fxColor(i,nx,ny,p[2]);
        ctx.globalAlpha=0.14+0.86*c.br; ctx.fillStyle=c.col;
        ctx.beginPath(); ctx.arc(pad+nx*sw, pad+(1-ny)*sh, 1.4+2.6*c.br, 0, 6.283); ctx.fill();
      }
    }
    ctx.globalAlpha=1;
  }
  renderOne(afCanvasLive, true);   // actual output playing now
  renderOne(afCanvasFx, false);    // audio-reactive effect
}
fetch(afApi('uploadlayout.php')+'&points=1').then(function(r){return r.json();}).then(function(d){
  if(d&&d.count>0&&d.pts){ afPts=d.pts; afGroups=d.groups||[];
    var gsel=document.getElementById('af-spatialgroup');
    if(gsel){ var cur=gsel.value; gsel.innerHTML='';
      ['(all)'].concat(afGroups).forEach(function(g){ var o=document.createElement('option'); o.value=g; o.textContent=g; if(g===cur)o.selected=true; gsel.appendChild(o); }); }
    var prevwrap=document.getElementById('af-prevwrap'), mon=document.getElementById('af-monitor');
    prevwrap.style.display='block';
    var ar=(d.ar&&d.ar>0)?d.ar:0.6;            // real width/height; two canvases side by side
    var vertical=ar<1, Wd, H;
    if(vertical){
      mon.style.flexDirection='row'; prevwrap.style.flex='0 0 auto';
      H=300; Wd=H*ar;
      var cap=Math.floor(((mon.clientWidth||560)-250)/2)-8; if(cap>50 && Wd>cap){ Wd=cap; H=Wd/ar; }
    } else {
      mon.style.flexDirection='column'; prevwrap.style.flex='1 1 100%';
      Wd=Math.floor((mon.clientWidth||540)/2)-10; H=Wd/ar; if(H>300){ H=300; Wd=H*ar; }
    }
    [afCanvasLive,afCanvasFx].forEach(function(cv){ if(cv){ cv.style.width=Math.round(Wd)+'px'; cv.style.height=Math.round(H)+'px'; } });
    afPrevLoop();
  }
}).catch(function(){});
// build band bars
var afBands = document.getElementById('af-bands');
for (var i=0;i<8;i++){ var d=document.createElement('div'); afBands.appendChild(d); }
// populate device list
function afAddOpt(sel,val,txt){ if(sel.value!==val){ var o=document.createElement('option'); o.value=val; o.textContent=txt; sel.appendChild(o); } }
fetch(afApi('devices.php')).then(function(r){return r.json();}).then(function(list){
  var sel=document.getElementById('af-device'); var cur=sel.value;
  afAddOpt(sel,'test','Test signal (no hardware)');
  afAddOpt(sel,'default','default');
  (list||[]).forEach(function(dev){ if(dev.id===cur)return; var o=document.createElement('option'); o.value=dev.id; o.textContent=dev.id+' — '+dev.name; sel.appendChild(o); });
}).catch(function(){ afAddOpt(document.getElementById('af-device'),'test','Test signal (no hardware)'); });
// live meters
function pct(v){ return Math.max(0,Math.min(100,Math.round(v*100)))+'%'; }
setInterval(function(){
  fetch(afApi('frame.php')).then(function(r){return r.text();}).then(function(t){
    if(!t){ window.afFrame=null; return; }
    var nl=t.indexOf('\n'); if(nl<0){ window.afFrame=null; return; }
    var hex=t.slice(nl+1).trim(), arr=[];
    for(var i=0;i+6<=hex.length;i+=6) arr.push([parseInt(hex.substr(i,2),16),parseInt(hex.substr(i+2,2),16),parseInt(hex.substr(i+4,2),16)]);
    window.afFrame=arr;
  }).catch(function(){ window.afFrame=null; });
  fetch(afApi('status.php')).then(function(r){return r.json();}).then(function(s){
    window.afLast=s;
    var dev=document.getElementById('af-dev'), txt=document.getElementById('af-devtxt');
    dev.className='dot'+(s.deviceOk?' on':''); txt.textContent = s.deviceOk ? (s.active?'hearing audio':'device open (silent)') : 'no audio device';
    document.getElementById('af-level').style.width=pct(s.level);
    document.getElementById('af-bass').style.width=pct(s.bass);
    document.getElementById('af-mid').style.width=pct(s.mid);
    document.getElementById('af-treble').style.width=pct(s.treble);
    document.getElementById('af-beat').className='dot'+(s.beat>0.5?' beat':(s.active?' on':''));
    document.getElementById('af-bpm').textContent = s.bpm>0 ? Math.round(s.bpm) : '–';
    var swd=document.getElementById('af-sw'), swt=document.getElementById('af-swtxt');
    if(swd){ if(s.switchEnabled){ swd.className='dot'+(s.switchOn?' on':' beat'); swt.textContent=s.switchOn?'switch ON':'switch OFF'; } else { swd.className='dot'; swt.textContent='not used'; } }
    var bars=afBands.children; (s.bands||[]).forEach(function(v,i){ if(bars[i]) bars[i].style.height=Math.max(2,Math.round(v*100))+'%'; });
    var hint = '';
    if(!s.deviceOk) hint = 'Tip: set the device (e.g. hw:1,0) and check it appears in the dropdown. Run "arecord -l" on the device to find it.';
    else if(window.afIdle){ var amb=document.getElementById('af-ambient-cb'); if(!amb||!amb.checked) hint = 'Idle — the designs (and auto design change) only run while a show is playing or Ambient mode is on. Start a show or turn on Ambient mode.'; }
    document.getElementById('af-hint').textContent = hint;
  }).catch(function(){ document.getElementById('af-devtxt').textContent='(plugin not running — enable it and restart fppd)'; });
}, 130);
// FPP only calls the plugin while it's outputting; track idle state (throttled) for the hint above
setInterval(function(){
  fetch('api/fppd/status').then(function(r){return r.json();}).then(function(s){
    window.afIdle = (s.status_name==='idle' || s.status===0);
  }).catch(function(){});
}, 2000);
</script>

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
      <div class="lab">Only while playing</div><div><?php echo afTog('onlyWhenPlaying', '1'); ?></div>
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
      <div class="lab">Mode</div><div><select id="af-spatialmode" onChange="SetPluginSetting('pixelpulse','spatial_mode',this.value,0,0);"><?php foreach (array('bloom','spectrum','vu','radial','pulse','spike','chase','sparkle','wave','fireworks','rain','strobe','colorwash','grow','spin','bars','ripple','fire','comet','plasma','scan','confetti') as $m) echo "<option value='$m'" . (af_get('spatial_mode','bloom')===$m?' selected':'') . ">$m</option>"; ?></select> <span class="help">22 styles, driven by physical LED position</span></div>
      <div class="lab">Model group</div><div><select id="af-spatialgroup" onChange="SetPluginSetting('pixelpulse','spatial_group',this.value,0,0);"><option value="<?php echo htmlspecialchars(af_get('spatial_group','(all)')); ?>"><?php echo htmlspecialchars(af_get('spatial_group','(all)')); ?></option></select> <span class="help">limit to one xLights group</span></div>
      <div class="lab">Auto design change</div><div><select onChange="SetPluginSetting('pixelpulse','spatial_autocycle',this.value,0,0);"><?php foreach (array('off','time','beats','smart') as $m) echo "<option value='$m'" . (af_get('spatial_autocycle','off')===$m?' selected':'') . ">$m</option>"; ?></select> <span class="help">smart = pick designs to match the music</span></div>
      <div class="lab">Change every</div><div><?php echo afNum('spatial_cyclesecs', '20', '3', '300', '1', 's'); ?></div>
      <div class="lab">Intensity</div><div><?php echo afNum('spatial_intensity', '100', '0', '200', '5', '%'); ?></div>
      <div class="lab"></div><div class="help">Upload your <b>xlights_rgbeffects.xml</b>. When enabled, this renders the whole display from the audio by each prop's real position — overriding the Range pipeline above.</div>
    </div></div>
  </div>
</div>

<script>
function afxToggle(cb){}
function afApi(p){ return 'plugin.php?plugin=pixelpulse&page=' + p + '&nopage=1'; }
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
var afSt={t:performance.now(),latch:false,ring:false,ringPh:0,chase:0,wave:0,spin:0,ripple:0,comet:0,scan:0,rain:[-1,-1,-1],bursts:[]};
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
  var trig=false; if(beat>0.5&&!afSt.latch){afSt.latch=true;trig=true;} if(beat<0.2)afSt.latch=false;
  afSt.chase+=dt*(0.12+0.5*lvl); afSt.wave+=dt*0.6;
  afSt.spin+=dt*(0.08+0.25*lvl); afSt.spin-=Math.floor(afSt.spin);
  afSt.ripple+=dt*(0.25+0.6*lvl); afSt.ripple-=Math.floor(afSt.ripple);
  afSt.comet+=dt*(0.22+0.5*lvl); afSt.comet-=Math.floor(afSt.comet);
  afSt.scan+=dt*(0.25+0.6*lvl); afSt.scan-=Math.floor(afSt.scan);
  if(trig){afSt.ring=true;afSt.ringPh=0;} if(afSt.ring){afSt.ringPh+=dt/0.6; if(afSt.ringPh>1.5)afSt.ring=false;}
  if(trig&&afSt.bursts.length<5){ var q=afPts[Math.floor(Math.random()*afPts.length)]; afSt.bursts.push({x:q[0],y:q[1],age:0}); }
  afSt.bursts.forEach(function(b){b.age+=dt;}); afSt.bursts=afSt.bursts.filter(function(b){return b.age<=1.2;});
  if(trig){ for(var r=0;r<afSt.rain.length;r++) if(afSt.rain[r]<0){afSt.rain[r]=1.05;break;} }
  for(var r2=0;r2<afSt.rain.length;r2++) if(afSt.rain[r2]>=0){afSt.rain[r2]-=dt/1.1; if(afSt.rain[r2]<-0.1)afSt.rain[r2]=-1;}
  var dom=0,dmax=0; for(var b3=0;b3<nb;b3++){ if((bands[b3]||0)>dmax){dmax=bands[b3];dom=b3;} }
  var chase=afSt.chase%1;
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
      default: var c1=Math.sin(i*12.9898)*43758.5453; c1-=Math.floor(c1); br=(c1<0.15+0.35*beat)?beat:0; var c2=Math.sin(i*78.233)*43758.5453; c2-=Math.floor(c2); hue=360*c2; break;
    }
    if(br<0)br=0; if(br>1)br=1; return {br:br, col:afHsv(hue,sat,Math.max(0.05,br))};
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
    var bars=afBands.children; (s.bands||[]).forEach(function(v,i){ if(bars[i]) bars[i].style.height=Math.max(2,Math.round(v*100))+'%'; });
    document.getElementById('af-hint').textContent = s.deviceOk ? '' : 'Tip: set the device (e.g. hw:1,0) and check it appears in the dropdown. Run "arecord -l" on the device to find it.';
  }).catch(function(){ document.getElementById('af-devtxt').textContent='(plugin not running — enable it and restart fppd)'; });
}, 130);
</script>

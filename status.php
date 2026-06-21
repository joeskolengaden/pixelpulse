<?php
// Serves the plugin's live analysis snapshot for the settings-page meters.
@header('Content-Type: application/json');
$f = '/tmp/pixelpulse_status.json';
if (is_file($f) && (time() - filemtime($f) < 5)) { readfile($f); }
else { echo '{"deviceOk":false,"active":false,"level":0,"beat":0,"bass":0,"mid":0,"treble":0,"bpm":0,"bands":[]}'; }

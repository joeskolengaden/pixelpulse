<?php
// Serves the plugin's live analysis snapshot for the settings-page meters.
@header('Content-Type: application/json');
// fppd writes to /dev/shm (shared across mount namespaces); /tmp is kept as a
// fallback. Apache runs under systemd PrivateTmp, so its /tmp differs from
// fppd's and would never see a /tmp status file.
$paths = array('/dev/shm/pixelpulse_status.json', '/tmp/pixelpulse_status.json');
foreach ($paths as $f) {
    if (is_file($f) && (time() - filemtime($f) < 5)) { readfile($f); exit; }
}
echo '{"deviceOk":false,"active":false,"level":0,"beat":0,"bass":0,"mid":0,"treble":0,"bpm":0,"bands":[]}';

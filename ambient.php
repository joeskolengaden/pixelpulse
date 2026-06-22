<?php
// Ambient mode setup: create a tiny black sequence + a looping playlist so FPP
// runs its output loop with no real show, letting the plugin drive the lights
// from live audio. ?setup=1 (re)creates them; the plugin itself starts/keeps
// the playlist running while idle (see ambientTick in AudioFxPlugin.cpp).
header('Content-Type: application/json');
$SEQ = '/home/fpp/media/sequences/pixelpulse_blank.fseq';
$PL  = '/home/fpp/media/playlists/pixelpulse_ambient.json';

// cover the uploaded layout's channels (so the display is black under the effect)
function pp_layout_max() {
    $f = '/home/fpp/media/config/pixelpulse_layout.txt'; $mx = 0;
    if (is_file($f)) {
        $h = fopen($f, 'r'); fgets($h); fgets($h);  // header + GROUPS
        while (($ln = fgets($h)) !== false) {
            $p = preg_split('/\s+/', trim($ln));
            if (count($p) >= 2) { $e = intval($p[0]) + intval($p[1]); if ($e > $mx) $mx = $e; }
        }
        fclose($h);
    }
    return $mx > 0 ? $mx : 13410;
}
// minimal uncompressed V1 FSEQ (28-byte header + raw zero channel data)
function pp_write_fseq($path, $ch, $frames, $step) {
    $hs = 28;
    $h = 'PSEQ' . pack('v', $hs) . chr(0) . chr(1) . pack('v', $hs)
       . pack('V', $ch) . pack('V', $frames) . chr($step) . chr(0)
       . pack('v', 0) . pack('v', 0) . chr(1) . chr(2) . pack('v', 0);
    $fh = fopen($path, 'wb'); if (!$fh) return false;
    fwrite($fh, $h);
    $z = str_repeat("\0", $ch);
    for ($i = 0; $i < $frames; $i++) fwrite($fh, $z);
    fclose($fh);
    return true;
}

if (isset($_GET['setup'])) {
    $ch = min(100000, pp_layout_max());
    $ok = pp_write_fseq($SEQ, $ch, 40, 50);   // 40 frames @ 50ms = 2s loop
    $pl = array(
        'name' => 'pixelpulse_ambient', 'version' => 3, 'repeat' => 1, 'loopCount' => 0,
        'empty' => false, 'desc' => 'Pixel Pulse ambient (blank loop for no-show audio reactivity)',
        'random' => 0, 'leadIn' => array(),
        'mainPlaylist' => array(array('type' => 'sequence', 'enabled' => 1, 'playOnce' => 0,
            'sequenceName' => 'pixelpulse_blank.fseq', 'duration' => 2)),
        'leadOut' => array(), 'playlistInfo' => array('total_duration' => 2, 'total_items' => 1));
    $okp = @file_put_contents($PL, json_encode($pl, JSON_PRETTY_PRINT)) !== false;
    echo json_encode(array('ok' => ($ok && $okp), 'channels' => $ch));
    exit;
}
echo json_encode(array('ok' => true, 'seq' => is_file($SEQ), 'playlist' => is_file($PL)));

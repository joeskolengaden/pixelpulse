<?php
// Serves the plugin's downsampled snapshot of the ACTUAL output colours (hex),
// so the settings page can preview what's really playing on the LEDs. Format:
// line 1 = count, line 2 = "RRGGBB..." per sampled LED (same order/stride as
// uploadlayout.php's points). Returns empty if stale (nothing playing).
header('Content-Type: text/plain');
$f = '/dev/shm/pixelpulse_frame.txt';
if (is_file($f) && (time() - filemtime($f) < 2)) { readfile($f); }

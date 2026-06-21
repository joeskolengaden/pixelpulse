<?php
// Lists ALSA capture devices (the USB audio input) by parsing `arecord -l`.
@header('Content-Type: application/json');
$out = array(); $lines = array();
@exec('arecord -l 2>/dev/null', $lines);
foreach ($lines as $l) {
    if (preg_match('/^card (\d+):\s*\S+\s*\[([^\]]*)\],\s*device (\d+):/', $l, $m)) {
        $out[] = array('id' => 'hw:' . $m[1] . ',' . $m[3], 'name' => trim($m[2]));
    }
}
echo json_encode($out);

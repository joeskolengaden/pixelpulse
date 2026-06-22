<?php
// Receives an xLights layout (xlights_rgbeffects.xml), extracts every positioned
// model -> absolute channel range + normalized position, and writes a compact
// flat file the plugin reads for spatial reactions. GET (no file) returns the
// current status so the settings page can show "N props loaded".
header('Content-Type: application/json');
$OUT = '/home/fpp/media/config/pixelpulse_layout.txt';

function pp_summary($path) {
    if (!is_file($path)) return array('ok' => true, 'count' => 0);
    $fh = fopen($path, 'r'); $first = fgets($fh); fclose($fh);
    $p = preg_split('/\s+/', trim($first));
    return array('ok' => true, 'count' => (count($p) >= 3 ? intval($p[2]) : 0), 'loaded' => true);
}

// GET ?points=1 -> the normalized positions, so the settings page can preview
// the active spatial mode without looking at the physical pixels.
if (isset($_GET['points'])) {
    $pts = array(); $ar = 0;
    if (is_file($OUT)) {
        $fh = fopen($OUT, 'r'); $hdr = fgets($fh);
        $h = preg_split('/\s+/', trim($hdr)); if (count($h) >= 4) $ar = floatval($h[3]);
        while (($ln = fgets($fh)) !== false) {
            $p = preg_split('/\s+/', trim($ln));
            if (count($p) >= 7) $pts[] = array(round($p[3], 3) + 0, round($p[4], 3) + 0, round($p[6], 3) + 0);
        }
        fclose($fh);
    }
    echo json_encode(array('count' => count($pts), 'ar' => $ar, 'pts' => $pts)); exit;
}

if (empty($_FILES['layout'])) { echo json_encode(pp_summary($OUT)); exit; }
if ($_FILES['layout']['error'] !== UPLOAD_ERR_OK) {
    echo json_encode(array('ok' => false, 'error' => 'upload failed')); exit;
}

libxml_use_internal_errors(true);
$xml = simplexml_load_file($_FILES['layout']['tmp_name']);
if ($xml === false) { echo json_encode(array('ok' => false, 'error' => 'not valid xLights XML')); exit; }

$models = $xml->xpath('//model');
$props = array(); $xs = array(); $ys = array(); $zs = array();
foreach ($models as $m) {
    $a = $m->attributes();
    if (!isset($a['WorldPosX'])) continue;
    $sc = (string)$a['StartChannel'];
    if (strpos($sc, ':') === false) continue;          // need "!Controller:N" form
    $start = intval(substr(strrchr($sc, ':'), 1));
    if ($start < 1) continue;
    $da = (string)$a['DisplayAs'];
    $stype = isset($a['StringType']) ? (string)$a['StringType'] : 'RGB Nodes';
    $per = (stripos($stype, 'RGBW') !== false) ? 4 : 3;
    if ($da === 'Custom') {
        $cells = preg_split('/[;,]/', (string)$a['CustomModel']);
        $nodes = 0; foreach ($cells as $c) { if (trim($c) !== '') $nodes++; }
    } else {
        $p1 = isset($a['parm1']) ? intval($a['parm1']) : 1;
        $p2 = isset($a['parm2']) ? intval($a['parm2']) : 1;
        $nodes = max(1, $p1) * max(1, $p2);
    }
    if ($nodes < 1) continue;
    $x = floatval($a['WorldPosX']); $y = floatval($a['WorldPosY']);
    $z = floatval(isset($a['WorldPosZ']) ? $a['WorldPosZ'] : 0);
    $props[] = array('s' => $start, 'c' => $nodes * $per, 'st' => $per, 'x' => $x, 'y' => $y, 'z' => $z);
    $xs[] = $x; $ys[] = $y; $zs[] = $z;
}
if (count($props) === 0) { echo json_encode(array('ok' => false, 'error' => 'no positioned models found')); exit; }

$minX = min($xs); $maxX = max($xs); $minY = min($ys); $maxY = max($ys); $minZ = min($zs); $maxZ = max($zs);
$cx = array_sum($xs) / count($xs); $cy = array_sum($ys) / count($ys);
$maxd = 0; foreach ($props as $p) { $dd = hypot($p['x'] - $cx, $p['y'] - $cy); if ($dd > $maxd) $maxd = $dd; }
if ($maxd <= 0) $maxd = 1;
function pp_nrm($v, $a, $b) { return ($b - $a) == 0 ? 0.0 : ($v - $a) / ($b - $a); }

$ar = ($maxY - $minY) != 0 ? ($maxX - $minX) / ($maxY - $minY) : 1.0;  // real width/height
$lines = array(sprintf('PIXELPULSE_LAYOUT 2 %d %.5f', count($props), $ar));
foreach ($props as $p) {
    $lines[] = sprintf('%d %d %d %.4f %.4f %.4f %.4f', $p['s'], $p['c'], $p['st'],
        pp_nrm($p['x'], $minX, $maxX), pp_nrm($p['y'], $minY, $maxY), pp_nrm($p['z'], $minZ, $maxZ),
        hypot($p['x'] - $cx, $p['y'] - $cy) / $maxd);
}
if (@file_put_contents($OUT, implode("\n", $lines) . "\n") === false) {
    echo json_encode(array('ok' => false, 'error' => 'cannot write ' . $OUT)); exit;
}
echo json_encode(array('ok' => true, 'count' => count($props),
    'bounds' => array('minX' => $minX, 'maxX' => $maxX, 'minY' => $minY, 'maxY' => $maxY)));

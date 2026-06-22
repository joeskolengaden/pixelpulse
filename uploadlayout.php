<?php
// Receives an xLights layout (xlights_rgbeffects.xml) and expands every model
// into its individual LED nodes with real world positions:
//   - Custom : nodes at their grid cells (CustomModel)
//   - Poly Line : nodes distributed by arc length along the point path
//       world = pointData * scale + worldPos   (xLights PolyPoint transform)
// Also reads modelGroups so a node carries a bitmask of the groups it's in.
// Writes a compact flat file the plugin reads for per-LED spatial reactions.
// GET ?points=1 -> positions+groups for the settings-page preview; GET (no
// file) -> status.
header('Content-Type: application/json');
$OUT = '/home/fpp/media/config/pixelpulse_layout.txt';

function pp_header($path) {  // -> array(count, ar, groups[])
    $r = array('count' => 0, 'ar' => 0, 'groups' => array());
    if (!is_file($path)) return $r;
    $fh = fopen($path, 'r');
    $h = preg_split('/\s+/', trim(fgets($fh)));
    if (count($h) >= 3) $r['count'] = intval($h[2]);
    if (count($h) >= 4) $r['ar'] = floatval($h[3]);
    $g = fgets($fh);
    if ($g !== false && strpos($g, 'GROUPS') === 0) {
        $rest = trim(substr($g, 6));
        if ($rest !== '') $r['groups'] = explode('|', $rest);
    }
    fclose($fh);
    return $r;
}

if (isset($_GET['points'])) {
    $hd = pp_header($OUT); $pts = array();
    if ($hd['count'] > 0 && is_file($OUT)) {
        $fh = fopen($OUT, 'r'); fgets($fh); fgets($fh);  // header + GROUPS
        $all = array();
        while (($ln = fgets($fh)) !== false) {
            $p = preg_split('/\s+/', trim($ln));
            if (count($p) >= 6) $all[] = array(floatval($p[1]), floatval($p[2]), floatval($p[4]), intval($p[5]));
        }
        fclose($fh);
        $stride = max(1, (int)ceil(count($all) / 1500));   // cap preview to ~1500 pts
        for ($i = 0; $i < count($all); $i += $stride) $pts[] = $all[$i];
    }
    echo json_encode(array('count' => $hd['count'], 'ar' => $hd['ar'], 'groups' => $hd['groups'], 'pts' => $pts));
    exit;
}

if (empty($_FILES['layout'])) {
    $hd = pp_header($OUT);
    echo json_encode(array('ok' => true, 'count' => $hd['count'], 'groups' => $hd['groups'], 'loaded' => $hd['count'] > 0));
    exit;
}
if ($_FILES['layout']['error'] !== UPLOAD_ERR_OK) { echo json_encode(array('ok' => false, 'error' => 'upload failed')); exit; }

libxml_use_internal_errors(true);
$xml = simplexml_load_file($_FILES['layout']['tmp_name']);
if ($xml === false) { echo json_encode(array('ok' => false, 'error' => 'not valid xLights XML')); exit; }

// model groups: ordered list of (name, set of member model names)
$groups = array();
foreach ($xml->xpath('//modelGroup') as $g) {
    $gm = (string)$g['models'];
    $groups[] = array('name' => (string)$g['name'], 'members' => $gm === '' ? array() : array_flip(explode(',', $gm)));
}

function pp_custom_cells($cm, &$ncols, &$nrows) {
    $cells = array(); $rows = explode(';', $cm); $nrows = count($rows); $ncols = 0;
    foreach ($rows as $ri => $row) {
        $cols = explode(',', $row); $ncols = max($ncols, count($cols));
        foreach ($cols as $ci => $c) { $c = trim($c); if ($c !== '') $cells[] = array(intval($c), $ci, $ri); }
    }
    return $cells;
}

$nodes = array();  // each: [channel, x, y, z, mask]
foreach ($xml->xpath('//model') as $m) {
    $a = $m->attributes();
    if (!isset($a['WorldPosX'])) continue;
    $sc = (string)$a['StartChannel']; if (strpos($sc, ':') === false) continue;
    $start = intval(substr(strrchr($sc, ':'), 1)); if ($start < 1) continue;
    $name = (string)$a['name'];
    $da = (string)$a['DisplayAs'];
    $wx = floatval($a['WorldPosX']); $wy = floatval($a['WorldPosY']); $wz = floatval(isset($a['WorldPosZ']) ? $a['WorldPosZ'] : 0);
    $sx = floatval(isset($a['ScaleX']) ? $a['ScaleX'] : 1); $sy = floatval(isset($a['ScaleY']) ? $a['ScaleY'] : 1);
    if ($sx == 0) $sx = 1; if ($sy == 0) $sy = 1;
    $per = (stripos((string)$a['StringType'], 'RGBW') !== false) ? 4 : 3;
    $mask = 0;
    foreach ($groups as $gi => $g) if (isset($g['members'][$name])) $mask |= (1 << $gi);

    if ($da === 'Custom') {
        $cells = pp_custom_cells((string)$a['CustomModel'], $ncols, $nrows);
        foreach ($cells as $cell) {
            list($order, $ci, $ri) = $cell;
            $lx = $ci - ($ncols - 1) / 2.0; $ly = ($nrows - 1) / 2.0 - $ri;
            $nodes[] = array($start + ($order - 1) * $per, $wx + $lx * $sx, $wy + $ly * $sy, $wz, $mask);
        }
    } else {  // Poly Line
        $pd = array_values(array_filter(explode(',', (string)$a['PointData']), function ($v) { return $v !== ''; }));
        $vs = array();
        for ($i = 0; $i + 2 < count($pd) + 1 && $i + 1 < count($pd); $i += 3)
            $vs[] = array(floatval($pd[$i]) * $sx + $wx, floatval($pd[$i + 1]) * $sy + $wy);
        if (count($vs) < 1) continue;
        $N = max(1, intval($a['parm1'])) * max(1, intval($a['parm2']));
        $seglen = array(); $total = 0;
        for ($i = 0; $i < count($vs) - 1; $i++) { $l = hypot($vs[$i + 1][0] - $vs[$i][0], $vs[$i + 1][1] - $vs[$i][1]); $seglen[] = $l; $total += $l; }
        if ($total <= 0) $total = 1;
        for ($j = 0; $j < $N; $j++) {
            $t = ($j + 0.5) / $N * $total; $acc = 0; $x = $vs[count($vs) - 1][0]; $y = $vs[count($vs) - 1][1];
            for ($i = 0; $i < count($seglen); $i++) {
                if ($acc + $seglen[$i] >= $t || $i == count($seglen) - 1) {
                    $f = $seglen[$i] > 0 ? ($t - $acc) / $seglen[$i] : 0; $f = max(0, min(1, $f));
                    $x = $vs[$i][0] + $f * ($vs[$i + 1][0] - $vs[$i][0]); $y = $vs[$i][1] + $f * ($vs[$i + 1][1] - $vs[$i][1]); break;
                }
                $acc += $seglen[$i];
            }
            $nodes[] = array($start + $j * $per, $x, $y, $wz, $mask);
        }
    }
}
if (count($nodes) === 0) { echo json_encode(array('ok' => false, 'error' => 'no positioned models found')); exit; }

$xs = array(); $ys = array(); $zs = array();
foreach ($nodes as $n) { $xs[] = $n[1]; $ys[] = $n[2]; $zs[] = $n[3]; }
$minX = min($xs); $maxX = max($xs); $minY = min($ys); $maxY = max($ys); $minZ = min($zs); $maxZ = max($zs);
$cx = array_sum($xs) / count($xs); $cy = array_sum($ys) / count($ys);
$maxd = 0; foreach ($nodes as $n) { $dd = hypot($n[1] - $cx, $n[2] - $cy); if ($dd > $maxd) $maxd = $dd; }
if ($maxd <= 0) $maxd = 1;
$ar = ($maxY - $minY) != 0 ? ($maxX - $minX) / ($maxY - $minY) : 1.0;
function pp_nrm($v, $a, $b) { return ($b - $a) == 0 ? 0.0 : ($v - $a) / ($b - $a); }

$gn = array(); foreach ($groups as $g) $gn[] = $g['name'];
$lines = array(sprintf('PIXELPULSE_LAYOUT 3 %d %.5f %d', count($nodes), $ar, count($groups)));
$lines[] = 'GROUPS ' . implode('|', $gn);
foreach ($nodes as $n) {
    $lines[] = sprintf('%d %.4f %.4f %.4f %.4f %d', $n[0],
        pp_nrm($n[1], $minX, $maxX), pp_nrm($n[2], $minY, $maxY), pp_nrm($n[3], $minZ, $maxZ),
        hypot($n[1] - $cx, $n[2] - $cy) / $maxd, $n[4]);
}
if (@file_put_contents($OUT, implode("\n", $lines) . "\n") === false) {
    echo json_encode(array('ok' => false, 'error' => 'cannot write ' . $OUT)); exit;
}
echo json_encode(array('ok' => true, 'count' => count($nodes), 'leds' => count($nodes), 'groups' => $gn,
    'bounds' => array('minX' => $minX, 'maxX' => $maxX, 'minY' => $minY, 'maxY' => $maxY)));

<?php
// Consolidated APP endpoint (reusable across plugins - drop this same file into
// any plugin that ships a pluginInfo.json with a settingsSchema).
//
// Returns, in one call, everything a mobile/remote app needs to render and drive
// this plugin's controls:
//   { plugin, name, type, groups[], settings[ {key,label,type,options,min,max,
//     unit,group,help,value} ], status{live}, setUrl }
//
// The app renders one control per setting (toggle/slider/dropdown by `type`),
// seeds it with `value`, POSTs changes to  setUrl + <key>  (value as the body),
// and polls this endpoint (or the plugin's status file) for live `status`.
//
//   GET /plugin.php?plugin=<name>&page=app.php&nopage=1
header('Content-Type: application/json');

$info = @json_decode(@file_get_contents(__DIR__ . '/pluginInfo.json'), true);
if (!is_array($info)) { echo json_encode(array('error' => 'pluginInfo.json missing/invalid')); exit; }

$repo   = isset($info['repoName']) ? $info['repoName'] : (isset($_GET['plugin']) ? $_GET['plugin'] : '');
$schema = isset($info['settingsSchema']) && is_array($info['settingsSchema']) ? $info['settingsSchema'] : array();

// current values: parse the plugin config file (lines: key = "value")
$mediaDir = '/home/fpp/media';
$cfgPath  = isset($info['settingsFile']) ? ($mediaDir . '/' . $info['settingsFile'])
                                         : ($mediaDir . '/config/plugin.' . $repo);
$vals = array();
if (is_file($cfgPath)) {
    foreach (file($cfgPath, FILE_IGNORE_NEW_LINES | FILE_SKIP_EMPTY_LINES) as $ln) {
        $p = strpos($ln, '=');
        if ($p === false) continue;
        $k = trim(substr($ln, 0, $p));
        $v = trim(substr($ln, $p + 1));
        if (strlen($v) >= 2 && $v[0] === '"' && substr($v, -1) === '"') $v = substr($v, 1, -1);
        if ($k !== '') $vals[$k] = $v;
    }
}

// attach the current value (or schema default) to each setting; collect groups
$groups = array();
foreach ($schema as &$s) {
    $k = isset($s['key']) ? $s['key'] : '';
    $s['value'] = array_key_exists($k, $vals) ? $vals[$k] : (isset($s['default']) ? $s['default'] : null);
    if (isset($s['group']) && !in_array($s['group'], $groups, true)) $groups[] = $s['group'];
}
unset($s);

// optional live status (convention: /dev/shm/<repo>_status.json)
$status = null;
$sf = '/dev/shm/' . $repo . '_status.json';
if (is_file($sf)) { $status = @json_decode(@file_get_contents($sf), true); }

echo json_encode(array(
    'plugin'   => $repo,
    'name'     => isset($info['name']) ? $info['name'] : $repo,
    'type'     => isset($info['pluginType']) ? $info['pluginType'] : '',
    'groups'   => $groups,
    'settings' => $schema,
    'status'   => $status,
    // POST the new value (raw, as the request body) to setUrl + <key>
    'setUrl'   => 'api/plugin/' . $repo . '/settings/',
));

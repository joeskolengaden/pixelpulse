<?php
// Menu entries for the audiofx plugin (FPP includes this per menu section).
$menuEntries = Array(
    Array('text' => 'Audio FX', 'type' => 'content', 'page' => 'plugin.php', 'wrap' => 1),
    Array('text' => 'Audio FX - Help', 'type' => 'help', 'page' => 'https://github.com/joeskolengaden/audiofx', 'wrap' => 1)
);
foreach ($menuEntries as $entry) {
    if ($entry['type'] != $menu) continue;
    if (preg_match('/^http.?:\/\//', $entry['page']))
        printf("<li><a href='%s' target='_blank'>%s</a></li>\n", $entry['page'], $entry['text']);
    else {
        $nopage = (isset($entry['wrap']) && $entry['wrap'] == 0) ? '&nopage=1' : '';
        printf("<li><a href='plugin.php?plugin=%s&page=%s%s'>%s</a></li>\n", $plugin, $entry['page'], $nopage, $entry['text']);
    }
}
?>

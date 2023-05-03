if (`SELECT @@have_dynamic_loading != 'YES'`) {
  --skip Example plugin requires dynamic loading
}

if (!$MYKIRUNA_PLUGIN) {
  --skip Could not find library for example plugin, see the file plugin.defs
}

if (`SELECT CONCAT('--plugin-dir=', REPLACE(@@plugin_dir, '\\\\', '/')) != '$MYKIRUNA_PLUGIN_OPT/'`) {
  --skip MyKiruna plugin requires that --plugin-dir is set to the MyKiruna plugin dir
}

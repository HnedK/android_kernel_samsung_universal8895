#!/sbin/sh
# E-Kernel 8895 Module — customize.sh
# Runs during module installation (Magisk/KernelSU)

SKIPUNZIP=1

ui_print "╔══════════════════════════════════════╗"
ui_print "║     E-Kernel 8895 Module v1.0        ║"
ui_print "║   Performance & Thermal Tweaks       ║"
ui_print "║   Samsung Galaxy S8/S8+/Note8        ║"
ui_print "╚══════════════════════════════════════╝"
ui_print ""

# Extract module files
ui_print "- Extracting module files..."
unzip -o "$ZIPFILE" -x 'META-INF/*' -d "$MODPATH" >&2

# Create config directory
CONFIGDIR="/data/ekernel/config"
mkdir -p "$CONFIGDIR"

# Set default configs if they don't exist
set_default() {
    local file="$CONFIGDIR/$1"
    local value="$2"
    if [ ! -f "$file" ]; then
        echo "$value" > "$file"
        ui_print "  Default: $1 = $value"
    else
        ui_print "  Existing: $1 = $(cat $file)"
    fi
}

ui_print ""
ui_print "- Setting default configurations..."
set_default "cpu_governor" "schedutil"
set_default "io_scheduler" "bfq"
set_default "read_ahead_kb" "512"
set_default "swappiness" "10"
set_default "dirty_ratio" "20"
set_default "dirty_background_ratio" "5"
set_default "vfs_cache_pressure" "80"
set_default "tcp_congestion" "bbr"
set_default "thermal_profile" "0"
set_default "store_mode" "0"

# Set permissions
ui_print ""
ui_print "- Setting permissions..."
set_perm_recursive "$MODPATH" 0 0 0755 0644
set_perm "$MODPATH/service.sh" 0 0 0755

ui_print ""
ui_print "- Installation complete!"
ui_print "  Config dir: /data/ekernel/config"
ui_print "  Edit configs and reboot to apply."
ui_print ""

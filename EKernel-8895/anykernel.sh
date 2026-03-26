# AnyKernel3 - E-Kernel 8895
# osm0sis @ xda-developers

## AnyKernel setup
properties() { '
kernel.string=E-Kernel 8895 by E-Kernel Team
do.devicecheck=0
do.modules=0
do.systemless=1
do.cleanup=1
do.cleanuponabort=0
device.name1=dreamlte
device.name2=dream2lte
device.name3=greatlte
device.name4=dreamlteks
device.name5=dream2lteks
supported.versions=
supported.patchlevels=
'; }

# Shell variables
block=/dev/block/platform/11120000.ufs/by-name/BOOT;
is_slot_device=0;
ramdisk_compression=auto;

ensure_ekernel_dirs() {
    mkdir -p /data/ekernel /data/ekernel/config 2>/dev/null;
}

set_default_config() {
    local name="$1";
    local value="$2";
    local file="/data/ekernel/config/$name";

    if [ ! -f "$file" ]; then
        printf '%s\n' "$value" > "$file";
    fi
}

init_module_defaults() {
    ensure_ekernel_dirs;
    set_default_config "cpu_governor" "schedutil";
    set_default_config "io_scheduler" "bfq";
    set_default_config "read_ahead_kb" "512";
    set_default_config "swappiness" "10";
    set_default_config "dirty_ratio" "20";
    set_default_config "dirty_background_ratio" "5";
    set_default_config "vfs_cache_pressure" "80";
    set_default_config "tcp_congestion" "bbr";
    set_default_config "thermal_profile" "0";
}

install_module_payload() {
    if [ ! -f "$AKHOME/ekernel-module.zip" ]; then
        ui_print "  Module payload missing: ekernel-module.zip";
        return;
    fi

    mkdir -p /data/adb/modules/ekernel-8895 2>/dev/null;
    if unzip -o "$AKHOME/ekernel-module.zip" -d /data/adb/modules/ekernel-8895 >/dev/null 2>&1; then
        chmod 0755 /data/adb/modules/ekernel-8895 2>/dev/null;
        chmod 0755 /data/adb/modules/ekernel-8895/service.sh 2>/dev/null;
        chmod 0644 /data/adb/modules/ekernel-8895/module.prop 2>/dev/null;
        chmod 0644 /data/adb/modules/ekernel-8895/system.prop 2>/dev/null;
        init_module_defaults;
        ui_print "  Module installed to /data/adb/modules/ekernel-8895";
        ui_print "  Default config created in /data/ekernel/config";
    else
        ui_print "  Module install failed";
    fi
}

stage_manager_apk() {
    if [ ! -f "$AKHOME/ekernel-app.apk" ]; then
        ui_print "  Manager APK missing: ekernel-app.apk";
        return;
    fi

    if cp "$AKHOME/ekernel-app.apk" /sdcard/ekernel-app.apk 2>/dev/null; then
        ui_print "  APK copied to /sdcard/ekernel-app.apk";
        ui_print "  Install it manually after boot";
    else
        ui_print "  APK could not be copied from recovery";
    fi
}

## AnyKernel methods (DO NOT CHANGE)
. tools/ak3-core.sh;

## AnyKernel install
[ -f /tmp/anykernel3/boot.img ] && AKHOME=/tmp/anykernel3;
[ -f /tmp/anykernel/boot.img ] && AKHOME=/tmp/anykernel;
[ -f "$AKHOME/boot.img" ] || abort "Missing boot.img payload. Aborting...";
ui_print "  Flashing boot image from $AKHOME/boot.img...";
dd if="$AKHOME/boot.img" of="$block" bs=4096 conv=fsync || abort "Flashing boot image failed. Aborting...";

## Post-install
ui_print " ";
ui_print "  Installing E-Kernel 8895 module...";
install_module_payload;

ui_print " ";
ui_print "  Staging E-Kernel Manager app...";
stage_manager_apk;

ui_print " ";
ui_print "  E-Kernel 8895 installed";
ui_print "  Reboot to activate";

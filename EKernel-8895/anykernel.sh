# AnyKernel3 - E-Kernel 8895
# osm0sis @ xda-developers

## AnyKernel setup
properties() { '
kernel.string=E-Kernel 8895 by E-Kernel Team
do.devicecheck=0
do.modules=0
do.systemless=0
do.cleanup=1
do.cleanuponabort=0
do.ramdisk=0
device.name1=greatlte
device.name2=greatltechn
device.name3=greatlteks
supported.versions=
supported.patchlevels=
'; }

# Shell variables
AKHOME="${AKHOME:-$(pwd)}";
OUTFD="${OUTFD:-/proc/self/fd/1}";

ui_print() {
    echo -n -e "ui_print $1\n" > "$OUTFD";
    echo -n -e "ui_print\n" > "$OUTFD";
}

abort() {
    ui_print " ";
    ui_print "$1";
    ui_print " ";
    exit 1;
}

find_boot_block() {
    $bb find /dev/block/platform -iname boot | $bb head -n1
}

resolve_kernel_payload() {
    if [ -f "$AKHOME/kernel" ]; then
        printf '%s\n' "$AKHOME/kernel"
        return 0
    fi

    if [ -f "$AKHOME/Image" ]; then
        printf '%s\n' "$AKHOME/Image"
        return 0
    fi

    if [ -f "$AKHOME/Image.gz-dtb" ]; then
        printf '%s\n' "$AKHOME/Image.gz-dtb"
        return 0
    fi

    return 1
}

resolve_extra_payload() {
    if [ -f "$AKHOME/extra" ]; then
        printf '%s\n' "$AKHOME/extra"
        return 0
    fi

    if [ -f "$AKHOME/dtb.img" ]; then
        printf '%s\n' "$AKHOME/dtb.img"
        return 0
    fi

    return 1
}

stage_payload_copy() {
    local source="$1"
    local staged="$2"

    [ -n "$source" ] || return 1
    cp "$source" "$staged" || return 1
}

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
    set_default_config "store_mode" "0";
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

## AnyKernel install
bb=$AKHOME/tools/busybox;
boot=$(find_boot_block);
[ -n "$boot" ] || abort "Could not locate boot partition. Aborting...";
[ -x "$AKHOME/tools/magiskboot" ] || abort "Missing magiskboot tool. Aborting...";
kernel_payload=$(resolve_kernel_payload) || abort "Missing kernel payload (expected kernel or Image.gz-dtb). Aborting...";
extra_payload=$(resolve_extra_payload || true);
staged_kernel="$AKHOME/.ak3-kernel";
staged_extra="$AKHOME/.ak3-extra";

if [ -n "$extra_payload" ] && [ -f "$AKHOME/Image.gz-dtb" ] && [ ! -f "$AKHOME/kernel" ] && [ ! -f "$AKHOME/Image" ]; then
    abort "Package mixes Image.gz-dtb with a separate extra payload. Aborting...";
fi

rm -f "$staged_kernel" "$staged_extra";
stage_payload_copy "$kernel_payload" "$staged_kernel" || abort "Kernel staging failed. Aborting...";
if [ -n "$extra_payload" ]; then
    stage_payload_copy "$extra_payload" "$staged_extra" || abort "Extra staging failed. Aborting...";
fi

cd "$AKHOME" || abort "Could not access installer workspace. Aborting...";
dd if="$boot" of="$AKHOME/old-boot.img" bs=4096 >/dev/null 2>&1 || abort "Boot dump failed. Aborting...";
./tools/magiskboot unpack "$AKHOME/old-boot.img" >/dev/null 2>&1 || abort "Boot unpack failed. Aborting...";
[ -f kernel ] || abort "Boot unpack did not produce a kernel blob. Aborting...";
rm -f kernel;
cp "$staged_kernel" kernel || abort "Kernel copy failed. Aborting...";
if [ -n "$extra_payload" ]; then
    [ -f extra ] || abort "Package contains extra payload but unpacked boot image has no extra blob. Aborting...";
    rm -f extra;
    cp "$staged_extra" extra || abort "Extra copy failed. Aborting...";
fi
./tools/magiskboot repack -n "$AKHOME/old-boot.img" >/dev/null 2>&1 || abort "Boot repack failed. Aborting...";
[ -f "$AKHOME/new-boot.img" ] || abort "Repack did not create new-boot.img. Aborting...";
dd if="$AKHOME/new-boot.img" of="$boot" bs=4096 >/dev/null 2>&1 || abort "Boot flash failed. Aborting...";
sync;

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

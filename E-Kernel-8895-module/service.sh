#!/sbin/sh
# E-Kernel 8895 Module — service.sh
# Runs at boot to apply tuning parameters from /data/ekernel/config

MODDIR=${0%/*}
CONFIGDIR="/data/ekernel/config"
LOGFILE="/data/ekernel/ekernel.log"

log() {
    echo "$(date '+%Y-%m-%d %H:%M:%S') [E-Kernel] $1" >> "$LOGFILE"
}

apply_param() {
    local node="$1"
    local value="$2"
    if [ -f "$node" ]; then
        echo "$value" > "$node" 2>/dev/null && log "Set $node = $value" || log "FAIL $node = $value"
    fi
}

log "=== E-Kernel 8895 service.sh started ==="

# Wait for boot to settle
sleep 10

# ========= CPU Governor =========
if [ -f "$CONFIGDIR/cpu_governor" ]; then
    GOV=$(cat "$CONFIGDIR/cpu_governor")
    for cpu in /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor; do
        apply_param "$cpu" "$GOV"
    done
fi

# ========= CPU Frequencies =========
if [ -f "$CONFIGDIR/cpu_max_freq_big" ]; then
    FREQ=$(cat "$CONFIGDIR/cpu_max_freq_big")
    for cpu in /sys/devices/system/cpu/cpu4/cpufreq/scaling_max_freq \
               /sys/devices/system/cpu/cpu5/cpufreq/scaling_max_freq \
               /sys/devices/system/cpu/cpu6/cpufreq/scaling_max_freq \
               /sys/devices/system/cpu/cpu7/cpufreq/scaling_max_freq; do
        apply_param "$cpu" "$FREQ"
    done
fi

if [ -f "$CONFIGDIR/cpu_max_freq_little" ]; then
    FREQ=$(cat "$CONFIGDIR/cpu_max_freq_little")
    for cpu in /sys/devices/system/cpu/cpu0/cpufreq/scaling_max_freq \
               /sys/devices/system/cpu/cpu1/cpufreq/scaling_max_freq \
               /sys/devices/system/cpu/cpu2/cpufreq/scaling_max_freq \
               /sys/devices/system/cpu/cpu3/cpufreq/scaling_max_freq; do
        apply_param "$cpu" "$FREQ"
    done
fi

# ========= I/O Scheduler =========
if [ -f "$CONFIGDIR/io_scheduler" ]; then
    SCHED=$(cat "$CONFIGDIR/io_scheduler")
    for queue in /sys/block/sd*/queue/scheduler /sys/block/mmcblk*/queue/scheduler; do
        apply_param "$queue" "$SCHED"
    done
fi

# ========= Read Ahead =========
if [ -f "$CONFIGDIR/read_ahead_kb" ]; then
    RA=$(cat "$CONFIGDIR/read_ahead_kb")
    for queue in /sys/block/sd*/queue/read_ahead_kb /sys/block/mmcblk*/queue/read_ahead_kb; do
        apply_param "$queue" "$RA"
    done
fi

# ========= VM Tuning =========
[ -f "$CONFIGDIR/swappiness" ] && apply_param /proc/sys/vm/swappiness "$(cat $CONFIGDIR/swappiness)"
[ -f "$CONFIGDIR/dirty_ratio" ] && apply_param /proc/sys/vm/dirty_ratio "$(cat $CONFIGDIR/dirty_ratio)"
[ -f "$CONFIGDIR/dirty_background_ratio" ] && apply_param /proc/sys/vm/dirty_background_ratio "$(cat $CONFIGDIR/dirty_background_ratio)"
[ -f "$CONFIGDIR/vfs_cache_pressure" ] && apply_param /proc/sys/vm/vfs_cache_pressure "$(cat $CONFIGDIR/vfs_cache_pressure)"

# ========= TCP Congestion =========
if [ -f "$CONFIGDIR/tcp_congestion" ]; then
    TCP=$(cat "$CONFIGDIR/tcp_congestion")
    apply_param /proc/sys/net/ipv4/tcp_congestion_control "$TCP"
fi

# ========= Thermal Profile =========
if [ -f "$CONFIGDIR/thermal_profile" ]; then
    PROFILE=$(cat "$CONFIGDIR/thermal_profile")
    apply_param /proc/ekernel/thermal_profile "$PROFILE"
fi

# ========= Battery Charge Limit / Store Mode =========
if [ -f "$CONFIGDIR/store_mode" ]; then
    apply_param /sys/class/power_supply/battery/store_mode "$(cat $CONFIGDIR/store_mode)"
fi

# ========= GPU Frequencies =========
if [ -f "$CONFIGDIR/gpu_max_freq" ]; then
    apply_param /sys/devices/platform/17500000.mali/max_clock "$(cat $CONFIGDIR/gpu_max_freq)"
fi
if [ -f "$CONFIGDIR/gpu_min_freq" ]; then
    apply_param /sys/devices/platform/17500000.mali/min_clock "$(cat $CONFIGDIR/gpu_min_freq)"
fi

log "=== E-Kernel 8895 service.sh completed ==="

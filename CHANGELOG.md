# E-Kernel 8895 — CHANGELOG

## v1.0 — 2026-03-23

### Task 1: Static Analysis & Bug Fixes

- **[FIX] exynos_tmu.c: Missing break in switch fallthrough (TEM1456X → TEM1455X)**
  - File: `drivers/thermal/samsung/exynos_tmu.c`
  - In `exynos8895_tmu_control()`, case `TEM1456X` was missing a `break` statement,
    causing unintended fallthrough to `TEM1455X`. This could result in incorrect MUX
    address configuration on TEM1456X sensor types.
  - Commit: `fix: add missing break in exynos8895_tmu_control switch (TEM1456X)`

- **[FIX] exynos_tmu.c: falling_threshold uninitialized read in exynos8890_tmu_initialize**
  - File: `drivers/thermal/samsung/exynos_tmu.c`
  - `falling_threshold` was read from register only for `rising_threshold` but reused
    with stale value for falling path. Added proper register read before modification.
  - Commit: `fix: read falling_threshold register before modifying in exynos8890_tmu_initialize`

### Task 2: Performance Tweaks

- Added `CONFIG_EKERNEL_THERMAL_PROFILES` Kconfig option
- Added `CONFIG_EKERNEL_CPU_OC` Kconfig option (default=n, gated optional OC)
- Updated VM sysctl defaults: `vm.swappiness=10`, `vm.dirty_ratio=20`, `vm.dirty_background_ratio=5`

### Task 3: Thermal Tweaks

- New driver: `drivers/thermal/samsung/ekernel_thermal.c`
  - Three thermal profiles: Balanced (0), Gaming (1), Cool (2)
  - Runtime switchable via `/proc/ekernel/thermal_profile`
  - Thermal stats via `/proc/ekernel/thermal_stats`
  - Kernel parameter: `ekernel.thermal_profile=0/1/2`
  - Hard safety ceiling: 108°C maximum trip point

### Task 4: KernelSU Integration (v0.9.5, Manual Method)

- Patched `fs/exec.c` — KSU execveat hooks in `do_execveat_common()`
- Patched `fs/open.c` — KSU faccessat hook in `SYSCALL_DEFINE3(faccessat)`
- Patched `fs/read_write.c` — KSU vfs_read hook
- Patched `fs/stat.c` — KSU vfs_fstatat hook
- Patched `fs/devpts/inode.c` — KSU devpts hook in `devpts_get_priv()`
- Patched `drivers/input/input.c` — KSU input handle event hook
- Patched `fs/namespace.c` — Backported `path_umount()` from Linux 5.9 with RKP_NS_PROT compat
- Downloaded KernelSU v0.9.5 source to `drivers/kernelsu/` (26 files)
- Integrated into build system: `drivers/Kconfig` + `drivers/Makefile`

### Task 5: Magisk/KernelSU Module

- Created `E-Kernel-8895-module/` directory with complete module structure
- `service.sh`: Boot-time parameter applier
- `customize.sh`: Installation script with default config generation

### Task 6: E-Kernel Manager App

- Created `ekernel-app/` Android project
- `MainActivity.java`: WebView wrapper with `EKernelBridge` JavaScript interface
- `assets/index.html`: Full single-page app with 6 tab panels (STATUS, CPU, GPU,
  THERMAL, GOVERNOR SETTINGS, SETTINGS)
- Dark theme (#0f0f0f / #00e5ff), responsive, zero external dependencies

### Task 7: AnyKernel3 Packaging

- Created `EKernel-8895/` packaging directory
- `anykernel.sh`: Device detection for dreamlte/dream2lte/greatlte
- Automatic app installation via `pm install`

### Task 8: Defconfig Additions

- Updated all three defconfigs with E-Kernel performance, thermal, and KernelSU options
- Targets: `exynos8895-dreamlte_defconfig`, `exynos8895-dream2lte_defconfig`,
  `exynos8895-greatlte_defconfig`

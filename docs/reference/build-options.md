# AkiraOS Build Options Reference

AkiraOS uses Zephyr's Kconfig system for build-time configuration. These options allow you to tailor the firmware's storage, features, hardware capabilities, and limitations to fit resource-constrained boards.

Below is a subset of the critical AkiraOS features (`CONFIG_AKIRA_*`) and runtime properties. Add these to your `prj.conf` or board-specific `.conf` file.

### OS & Core Settings

| Config Flag | Default | Description |
|---|---|---|
| `CONFIG_AKIRA_OS` | `y` | Enable overall AkiraOS features |
| `CONFIG_AKIRA_LOG_LEVEL` | `3` | Log level: `0`=OFF, `1`=ERR, `2`=WRN, `3`=INF, `4`=DBG |
| `CONFIG_AKIRA_TERMINAL_MODE` | `n` | Enable hacker terminal with network tools |
| `CONFIG_AKIRA_CYBERPUNK_THEME`| `n` | Enable cyberpunk UI theme (LVGL) |

### Runtime & WebAssembly 

| Config Flag | Default | Description |
|---|---|---|
| `CONFIG_MAX_CONTAINERS` | `8` | Max concurrent loaded WASM apps. (Increase needs more heap). |
| `CONFIG_AKIRA_WASM_RUNTIME` | `n` | Enable WebAssembly runtime (WAMR). Usually defaults to `y` on standard boards. |
| `CONFIG_WAMR_AOT_SUPPORT` | `n` | Enable WASM AOT compilation. Extremely recommended for CPU-heavy tasks (~40x faster) but costs flash space. |
| `CONFIG_WAMR_HEAP_SIZE` | `262144`| WAMR global heap memory threshold. |
| `CONFIG_AKIRA_WASM_APP_STACK_SIZE`| `8192`| Stack size per WASM app thread (SRAM only). |
| `CONFIG_AKIRA_APP_MANAGER` | `y` | Enable App Manager for lifecycle (install/run WASM apps). |

### Connectivity & Network Options

| Config Flag | Default | Description |
|---|---|---|
| `CONFIG_AKIRA_WIFI` | `y`* | Enable WiFi subsystem if hardware supports it. |
| `CONFIG_AKIRA_HTTP_SERVER` | `y` | Enable HTTP server for REST API and app uploads. |
| `CONFIG_AKIRA_WS_CLIENT` | `n` | Enable WebSocket client. |
| `CONFIG_AKIRA_CLOUD_CLIENT` | `y` | Enable unified cloud client. |
| `CONFIG_AKIRA_OTA` | `y` | Enable OTA firmware updates orchestration. |
| `CONFIG_AKIRA_MESH` | `n` | Enable AkiraMesh custom protocol routing. |
| `CONFIG_AKIRA_THREAD` | `n` | Enable OpenThread mesh (requires 802.15.4 chip like nRF). |

### Security & Privilege 

| Config Flag | Default | Description |
|---|---|---|
| `CONFIG_AKIRA_CAPABILITY_SYSTEM`| `y` | Enable capability-based security. Mandatory for safe WASM execution. |
| `CONFIG_AKIRA_APP_SIGNING` | `n` | Require WASM app signature verification (RSA/Ed25519). |
| `CONFIG_AKIRA_SANDBOX_RATE_LIMITING`| `y` | Throttle aggressive syscall usage by apps to prevent DOS. |
| `CONFIG_AKIRA_RESOURCE_MANAGER` | `y` | Enable resource (RAM/Flash) quota management per app. |

### API / Capabilities Exposure

Toggle these flags to export particular native APIs into the WASM sandboxes. If disabled, apps requiring them will fail capability checks.

| Config Flag | Default | API Domain |
|---|---|---|
| `CONFIG_AKIRA_WASM_API` | `y` | Enable baseline native API exports. |
| `CONFIG_AKIRA_WASM_POWER` | `y` | Power sensing API. |
| `CONFIG_AKIRA_WASM_POWER_CONTROL`| `n` | Restricted power control API (e.g., Deep Sleep commands). |
| `CONFIG_AKIRA_WASM_HID` | `n` | Export Human Interface Device API to WASM. |
| `CONFIG_AKIRA_WASM_LIFECYCLE` | `n` | Export app manipulation API (`app_start`, `app_stop`). |
| `CONFIG_AKIRA_WASM_IPC` | `n` | Export Pub/Sub IPC API. |

### Storage & Settings

| Config Flag | Default | Description |
|---|---|---|
| `CONFIG_AKIRA_SETTINGS` | `y` | Enable Settings Manager (NVS key-value store). |
| `CONFIG_AKIRA_SD_CARD` | `n` | Mount SD Card filesystem integration. |

For detailed constraints (like SRAM/PSRAM boundaries), see the [Architecture Overview](../architecture/system-overview.md).
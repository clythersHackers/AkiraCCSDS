# OTA Updates Guide

Over-the-air firmware update procedures for AkiraOS.

## Overview

AkiraOS uses MCUboot for secure OTA updates with rollback protection.

## Update Process

### 1. Build Firmware

```bash
cd ~/akira-workspace/AkiraOS
./build.sh -b esp32s3_devkitm_esp32s3_procpu
```

### 2. Upload via HTTP

```bash
curl -X POST \
  -F "firmware=@../build/zephyr/zephyr.bin" \
  http://192.168.1.100/ota/upload
```

### 3. Device Reboots

MCUboot verifies and swaps firmware images.

### 4. Verification

Check new version:
```bash
uart:~$ kernel version
```

## Rollback Protection

If new firmware fails to boot, MCUboot automatically rolls back to previous version.

### MCUboot Advanced Architecture

AkiraOS relies on **MCUboot** for secure boot and OTA updates using a dual-slot flash architecture. 

**Slot Mechanics**: 
- The Internal memory map contains a `Primary Slot` and a `Secondary Slot`.
- New firmware fragments passed over HTTP or BLE are continuously buffered via the `OTA Manager` and written precisely into the `Secondary Slot`.
- Once the digest matches, MCUboot sets an `image_ok` flag and signals a reboot swap.

**Fallback and Recovery**: 
- MCUboot will temporarily swap `Secondary` to `Primary` and attempt a boot. 
- If the image contains a severe exception (e.g. Zephyr Kernel Panic, Watchdog timeout) before the OS can validate itself by confirming `boot_confirm()`, MCUboot will automatically rollback by swapping the images back to original states on the subsequent hard reset. 
- You are protected against power outages during the erase/write stages automatically due to transaction headers.

## Security

- RSA/ECDSA signature verification
- Encrypted flash (optional)
- Version anti-rollback

## Related Documentation

- [OTA Manager Architecture](../architecture/connectivity.md#ota-manager)
- [MCUboot Documentation](https://mcuboot.com)
- [Security Model](../architecture/security.md)

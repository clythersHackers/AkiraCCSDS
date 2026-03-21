# STM32 Platform Guide

Experimental support for STM32 microcontrollers.

## Status

**Status: Experimental** — limited testing, community-driven

## Supported Boards

- STM32F4 Discovery
- STM32F7 Discovery
- Nucleo boards (select models)
- Custom boards (with porting)

## Features

- Cortex-M4/M7 ARM architecture
- External SRAM required for multiple WASM apps
- Rich peripherals — I2C, SPI, ADC, timers
- WiFi/BT via external modules

## Getting Started

### Build

```bash
cd ~/akira-workspace/AkiraOS
west build -b stm32f4_disco
```

### Flash

```bash
west flash  # via ST-Link
```

---

## Configuration

External SRAM required for WASM apps:

```bash
CONFIG_EXTERNAL_SRAM=y
CONFIG_HEAP_MEM_POOL_SIZE=131072  # 128KB
```

---

## Limitations

- Requires external PSRAM/SRAM
- WiFi/BT needs add-on modules
- Less testing than ESP32/nRF platforms

**Community Contributions Welcome!**

---

## Related Documentation

- [Platform Overview](index.md)
- [Building Apps](../development/building-apps.md)
- [Development Guide](../development/index.md)

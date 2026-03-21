# Debugging Guide

Debugging techniques for AkiraOS development.

## Logging

### Enable Debug Logs

```bash
CONFIG_LOG_DEFAULT_LEVEL=4  # DEBUG
```

### Runtime Log Control

```bash
AkiraOS:~$ log enable akira 4
AkiraOS:~$ log enable wasm 4
AkiraOS:~$ log list
```

## GDB Debugging (Native Sim)

```bash
# Build with debug symbols
west build -b native_sim -- -DCMAKE_BUILD_TYPE=Debug

# Run under GDB
gdb ../build/zephyr/zephyr.exe
(gdb) break main
(gdb) run
```

## JTAG Debugging (ESP32-S3)

```bash
# OpenOCD + GDB
west debug
```

## Core Dumps

Enable core dumps on crash:

```bash
CONFIG_DEBUG_COREDUMP=y
CONFIG_DEBUG_COREDUMP_BACKEND_LOGGING=y
```

## Memory Debugging

### Check Stack Usage

```bash
AkiraOS:~$ kernel stacks
AkiraOS:~$ kernel threads
```

### Enable Stack Canaries

```bash
CONFIG_STACK_CANARIES=y
```

## Related Documentation

- [Troubleshooting Guide](../getting-started/troubleshooting.md)
- [Native Simulation](../platform/native-sim.md)

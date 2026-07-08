# AkiraCCSDS

AkiraCCSDS is a fork of [AkiraOS](https://github.com/ArturR0k3r/AkiraOS).

AkiraOS is a Zephyr-based embedded operating system for microcontrollers that
runs sandboxed WebAssembly applications. The upstream project focuses on keeping
device firmware stable while applications are delivered as isolated `.wasm`
modules that can be installed or updated without reflashing the OS.

AkiraCCSDS keeps that base intact and adds experimental CCSDS telemetry,
telecommand, and file-transfer support under `src/connectivity/ccsds`.

## Why This Is Interesting

Default AkiraOS application upload and management flows are HTTP-oriented. That
is convenient for terrestrial development, but it is not how spacecraft are
normally commanded, monitored, or loaded with files.

AkiraCCSDS explores a different control path: use CCSDS-style telemetry and
telecommand for command and control, and use CFDP for file uplink and downlink.
In other words, keep the useful AkiraOS runtime model, but operate it through
protocols that look more like a space operations link.

The longer-term integration idea is to connect AkiraOS pub/sub messaging to
ground-side pub/sub protocols such as NATS or MQTT. Ground software could then
publish commands or data products into a secure messaging fabric, while the
onboard side receives only the CCSDS/CFDP traffic appropriate for the embedded
link and application model.

![IoT and CCSDS architecture sketch](src/connectivity/ccsds/IoTwithCCSDS.svg)

That may be simpler than trying to tunnel general IP over CCSDS. Instead of
making the spacecraft link pretend to be an IP network, AkiraCCSDS can map
application-level commands, telemetry, files, and messages onto the CCSDS
services that are already meant for constrained, delayed, or radio-carried
operations.


## Fork Focus

The CCSDS work in AkiraCCSDS is scoped to embedded, transport-neutral protocol
support:

- CCSDS Space Packet encode/decode helpers.
- TM and TC transfer-frame support.
- CLTU, BCH, Reed-Solomon, CRC-16, and optional randomization primitives.
- APID routing for decoded Space Packets.
- CFDP PDU, checksum, entity, range, and staging support.
- Development shell commands for manually starting and inspecting TM/TC paths.
- Development UDP adapters for TM output and TC input.
- Interfaces that allow CCSDS I/O to be registered through different device
  transports, such as UDP, UART, RF, CAN, BLE, or other board-specific links.

The implementation is intentionally kept in the AkiraOS connectivity layer
rather than redesigning the OS around CCSDS.

## Repository Layout

Key fork-specific files:

| Path | Purpose |
| ---- | ------- |
| `src/connectivity/ccsds/` | CCSDS protocol implementation |
| `src/connectivity/ccsds/USER_MANUAL.md` | Supported CCSDS shell workflows |
| `configs/ccsds.conf` | Opt-in CCSDS Kconfig fragment used by `build.sh -ccsds` |

Upstream AkiraOS documentation remains relevant for the base OS, WASM runtime,
SDK, board support, and general build system:

- [AkiraOS documentation](https://docs.akiraos.dev)
- [Upstream AkiraOS repository](https://github.com/ArturR0k3r/AkiraOS)

## Build

The known target for AkiraCCSDS is:

```text
Board: akiraconsole_esp32s3_procpu
Host: Fedora Linux
Environment: VS Code devcontainer
```

Build the AkiraConsole firmware:

```bash
./build.sh -b akiraconsole
```

Build with CCSDS enabled:

```bash
./build.sh -b akiraconsole -ccsds
```

The `-ccsds` flag applies `configs/ccsds.conf`, which currently enables:

```text
CONFIG_AKIRA_CCSDS=y
CONFIG_AKIRA_CCSDS_CFDP=y
```

## CCSDS Shell Quick Reference

When CCSDS shell support is enabled, TM output is manually controlled. Booting
the board does not automatically start telemetry.

Basic TM workflow:

```text
ccsds tm init
ccsds tm route info
ccsds tm route set 0 log
ccsds tm route set 7 log
ccsds tm start
ccsds tm status
ccsds tm stop
```

Development UDP TC input treats each received datagram as one complete CLTU:

```text
ccsds tc start udp
ccsds tc status
ccsds tc stop udp
```

See [src/connectivity/ccsds/USER_MANUAL.md](src/connectivity/ccsds/USER_MANUAL.md)
for the supported user-facing CCSDS shell behavior.

## Development Notes

AkiraCCSDS follows the upstream AkiraOS and Zephyr structure. CCSDS changes
should stay narrowly scoped to `src/connectivity/ccsds` and the minimal build
integration needed to compile that feature.

For embedded code in AkiraCCSDS:

- Use assertions for programmer contract violations when callers have no useful
  runtime recovery path.
- Use error returns for recoverable runtime conditions or normal variable input.
- Prefer `void` for functions that do not have meaningful runtime errors to
  report.
- Keep device I/O transport adapters separate from protocol codecs.

## License

AkiraCCSDS retains the upstream AkiraOS licensing and copyright notices. See
[LICENSE](LICENSE) and the upstream project for the original project context.

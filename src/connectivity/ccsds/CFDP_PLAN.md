# CFDP_PLAN.md — AkiraOS CFDP-lite Implementation Plan

**Target:** constrained C implementation for Zephyr/AkiraOS, initially suitable for an ESP32/STM32-class MCU.

**Goal:** replace HTTP-based WASM/application-file transfer with a small CCSDS-realistic file transfer layer.

**Protocol target:** CCSDS CFDP subset based on CCSDS 727.0-B-5:

- **v0a:** codec-only CFDP subset: fixed header, LV/TLV, Metadata, File Data, EOF, Finished, and checksum.
- **v0b:** Class 1, unacknowledged transfer with transaction closure, one static local/remote pair, and one active transaction.
- **v0c:** CCSDS Space Packet APID adapter after the Class 1 loopback path is proven.
- **v1:** limited Class 2-style reliable transfer using EOF-triggered missing-range NAK recovery.
- **Non-goal:** full CFDP, BP/DTN, IP-over-space, proxy operations, directory operations, store-and-forward overlay.

This plan is deliberately broken into small sessions. Each session should leave the tree compiling and tests passing.

---

## Scope Lock

### Implement now

Keep the near-term work MCU-first. Land the codec before the transaction
engine, and land the transaction engine before any CCSDS Space Packet adapter.

#### v0a codec

- Fixed PDU header encode/decode.
- LV/TLV helpers.
- Metadata PDU.
- File Data PDU.
- EOF PDU.
- Finished PDU.
- 32-bit modular checksum.
- Optional null checksum, mainly for testing.

#### v0b Class 1 transfer

- One local entity and one remote entity initially.
- One active transaction initially.
- File size using normal 32-bit FSS fields initially.
- Fixed segment size.
- Streaming checksum while sending; do not require a second pass over the file.
- Offset-based receive writes.
- Temporary-file receive and atomic rename/commit where the filesystem supports it.
- Small event/fault callback so completion and cleanup are observable.

#### v0c CCSDS integration

- CCSDS Space Packet APID adapter.
- One CFDP PDU per Space Packet, with segment size constrained so CFDP does not need to fragment.

### Explicitly defer

- NAK PDU.
- ACK PDU.
- Class 2 recovery.
- Retry timers beyond the minimal Class 1 closure timeout.
- Large-file mode / 64-bit FSS.
- Segment metadata.
- Record-boundary preservation.
- Filestore request TLVs.
- Proxy operations.
- Directory operations.
- Remote status/suspend/resume operations.
- Store-and-forward overlay operations.
- Multiple simultaneous transactions.
- Persistence across reboot.
- Prompt PDU.
- Keep Alive PDU.
- Full MIB machinery.
- Full fault-handler configurability.
- SDLS implementation; assume it lives below CFDP.
- Artifact signature verification; that belongs in the Akira installer above CFDP.

### Standard references to keep nearby

Local PDF: `src/connectivity/ccsds/727x0b5e1.pdf` (ignored by git) 
Also available as text `src/connectivity/ccsds/727x0b5e1.txt`

- Architecture and UT layer: CCSDS 727.0-B-5 §2.2.
- General characteristics: §2.3.
- CRC/checksum procedures: §4.1–§4.2.
- Put/copy-file procedures: §4.3–§4.6.
- Positive acknowledgement: §4.7.
- Fault handling: §4.8.
- Inactivity monitor: §4.10.
- PDU formats: §5, especially tables 5-1 to 5-14.
- Class 1 procedures: §7.2, tables 7-1 and 7-2.
- Class 2 procedures: §7.3, tables 7-3 and 7-4.

---

## Proposed Source Tree

Use the existing AkiraOS CCSDS layout. Do not create a parallel `src/cfdp`
tree unless the whole CCSDS stack is later reorganized.

```text
src/connectivity/ccsds/
  ccsds_cfdp_config.h
  ccsds_cfdp_types.h
  ccsds_cfdp_status.h
  ccsds_cfdp_pdu.h
  ccsds_cfdp_pdu.c
  ccsds_cfdp_checksum.h
  ccsds_cfdp_checksum.c
  ccsds_cfdp_ranges.h
  ccsds_cfdp_ranges.c
  ccsds_cfdp_filestore.h
  ccsds_cfdp_entity.h
  ccsds_cfdp_entity.c
  ccsds_cfdp_space_packet.h // v0c
  ccsds_cfdp_space_packet.c // v0c

tests/src/
  test_ccsds_cfdp_pdu.c
  test_ccsds_cfdp_checksum.c
  test_ccsds_cfdp_ranges.c
  test_ccsds_cfdp_class1.c
  test_ccsds_cfdp_space_packet.c // v0c
  test_ccsds_cfdp_nak_recovery.c // v1

src/connectivity/ccsds/
  CFDP_PLAN.md
  CFDP_SUBSET_NOTES.md       // optional, only if needed
  CFDP_TEST_VECTORS.md       // optional, only if needed
```

Build integration should follow the existing `CONFIG_AKIRA_CCSDS` pattern in
`CMakeLists.txt` and `Kconfig`. Add `CONFIG_AKIRA_CCSDS_CFDP` and only compile
CFDP sources when it is enabled.

---

## Core Design Rules

1. **No heap allocation in the codec.** Decode into caller-provided structs or views.
2. **All decode functions are bounds-checked.** No field is read before validating remaining length.
3. **No content logic inside CFDP.** CFDP delivers files only.
4. **No CCSDS link assumptions inside CFDP.** Use a UT callback.
5. **Keep sender and receiver state machines separate.** They may later coexist in one entity.
6. **Use offset-based file writes from day one.** Do not assume in-order File Data PDUs.
7. **Treat transaction ID as source entity ID + transaction sequence number.** Destination entity ID is part of the PDU header but not the unique transaction ID by itself.
8. **All unknown or unsupported optional features fail cleanly.** Do not silently accept things you do not implement.
9. **Compile after every stage.** Each session should be small enough to review.
10. **Prefer static caller-owned storage.** If a buffer or table is needed, make its size Kconfig-backed and visible in the owning object.
11. **Do not add background threads for CFDP v0.** Drive receive from input callbacks and drive timeouts from explicit polling.
12. **Avoid protocol-general abstractions until a second transport or second peer exists.** Efficiency and small state are more important than perfect modularity.

---

## Stage 1 — Constants, enums, and compile skeleton

Create:

- `ccsds_cfdp_config.h`
- `ccsds_cfdp_types.h`
- `ccsds_cfdp_status.h`

Include:

```c
#define CCSDS_CFDP_VERSION_1              1u
#define CCSDS_CFDP_MAX_ENTITY_ID_LEN      CONFIG_AKIRA_CCSDS_CFDP_MAX_ENTITY_ID_LEN
#define CCSDS_CFDP_MAX_TRANS_SEQ_LEN      CONFIG_AKIRA_CCSDS_CFDP_MAX_TRANS_SEQ_LEN
#define CCSDS_CFDP_MAX_FILENAME_LEN       CONFIG_AKIRA_CCSDS_CFDP_MAX_FILENAME_LEN
#define CCSDS_CFDP_MAX_PDU_SIZE           CONFIG_AKIRA_CCSDS_CFDP_MAX_PDU_SIZE
#define CCSDS_CFDP_MAX_SEGMENT_SIZE       CONFIG_AKIRA_CCSDS_CFDP_MAX_SEGMENT_SIZE
#define CCSDS_CFDP_MAX_NAK_RANGES         CONFIG_AKIRA_CCSDS_CFDP_MAX_NAK_RANGES
#define CCSDS_CFDP_MAX_ACTIVE_TX          1u
#define CCSDS_CFDP_MAX_ACTIVE_RX          1u
```

Recommended default Kconfig values:

```text
CONFIG_AKIRA_CCSDS_CFDP_MAX_ENTITY_ID_LEN=4
CONFIG_AKIRA_CCSDS_CFDP_MAX_TRANS_SEQ_LEN=4
CONFIG_AKIRA_CCSDS_CFDP_MAX_FILENAME_LEN=64
CONFIG_AKIRA_CCSDS_CFDP_MAX_PDU_SIZE=512
CONFIG_AKIRA_CCSDS_CFDP_MAX_SEGMENT_SIZE=384
CONFIG_AKIRA_CCSDS_CFDP_MAX_NAK_RANGES=4
```

These are defaults, not protocol limits. Boards with more RAM can raise them.
Keep `MAX_SEGMENT_SIZE + CFDP header/directive overhead` below the configured
CCSDS Space Packet payload limit.

Enums to define:

- PDU type: file directive, file data.
- Direction: toward receiver, toward sender.
- Transmission mode: acknowledged, unacknowledged.
- Directive codes: EOF, Finished, ACK, Metadata, NAK.
- Condition codes: enough for no-error, checksum failure, file-size error, inactivity, NAK limit, unsupported checksum, cancel, filestore rejection, invalid mode.
- Delivery code: complete/incomplete.
- File status: retained/discarded/status unreported.
- Checksum type: modular = 0, 2 = crc32c. 3 = crc32ieee, null = 15 (include algorithms already available in Zephyr)

Acceptance criteria:

- Headers compile standalone.
- No implementation logic yet.
- No dependency on Zephyr headers except optional config guards and
  `CONFIG_AKIRA_CCSDS_CFDP_*` fallbacks.
- CMake and Kconfig include a disabled-by-default CFDP option.

Suggested Codex prompt:

```text
Implement only the CCSDS CFDP constants, enums, Kconfig-backed limits, and status codes for the subset described in src/connectivity/ccsds/CFDP_PLAN.md. Do not implement any PDU parsing yet. Keep the headers standalone C99-compatible and avoid heap allocation.
```

---

## Stage 2 — Fixed header encode/decode

Implement:

- `ccsds_cfdp_pdu_header_t`
- `ccsds_cfdp_encode_header()`
- `ccsds_cfdp_decode_header()`
- `ccsds_cfdp_header_encoded_len()`

Fields to support:

- Version.
- PDU type.
- Direction.
- Transmission mode.
- CRC flag.
- Large-file flag.
- PDU data field length.
- Segmentation control.
- Entity ID length.
- Segment metadata flag.
- Transaction sequence number length.
- Source entity ID.
- Transaction sequence number.
- Destination entity ID.

v0 constraints:

- Reject version other than 1.
- Reject entity ID length > `CCSDS_CFDP_MAX_ENTITY_ID_LEN`.
- Reject transaction sequence number length > `CCSDS_CFDP_MAX_TRANS_SEQ_LEN`.
- Reject large-file flag initially unless explicitly configured.
- Accept CRC flag but do not verify until CRC stage; for early v0, reject CRC-present PDUs to avoid lying.
- Decode variable-length integers as big-endian unsigned values.

Acceptance criteria:

- Unit tests for 1-, 2-, and 4-octet entity IDs and transaction sequence numbers.
- Tests for malformed/truncated headers.
- Tests for unsupported version and unsupported large-file flag.

Suggested Codex prompt:

```text
Implement ccsds_cfdp_pdu_header_t plus fixed CFDP PDU header encode/decode only. Use the subset rules in src/connectivity/ccsds/CFDP_PLAN.md. Add tests for 1, 2, and 4 octet entity IDs and transaction sequence numbers. Do not implement directive PDUs yet.
```

---

## Stage 3 — Length-value and type-length-value helpers

Implement:

- `ccsds_cfdp_lv_read()`
- `ccsds_cfdp_lv_write()`
- `ccsds_cfdp_tlv_read()`
- `ccsds_cfdp_tlv_write()`

v0 use:

- LV is needed for source and destination filenames in Metadata PDU.
- TLV parser is needed so unknown Metadata options can be skipped or rejected.
- No filestore request TLVs in v0.

Rules:

- LV length is one octet.
- TLV type is one octet; TLV length is one octet.
- Never copy unless the caller asks; prefer pointer+length views.
- Reject overlength filenames at the Metadata layer.

Acceptance criteria:

- Tests for zero-length LV.
- Tests for max-length LV.
- Tests for truncated LV/TLV.
- Tests for multiple TLVs in a buffer.

Suggested Codex prompt:

```text
Implement bounded LV/TLV read/write helpers for CFDP. Return pointer+length views for decode. Add unit tests for zero length, normal length, max length, and truncation. Do not add filestore-specific semantics.
```

---

## Stage 4 — Metadata encode/decode

Implement:

- `ccsds_cfdp_metadata_pdu_t`
- `ccsds_cfdp_encode_metadata()`
- `ccsds_cfdp_decode_metadata()`

Fields:

- Closure requested.
- Checksum type.
- File size.
- Source file name LV.
- Destination file name LV.
- Options TLVs, initially parsed as opaque/skipped.

v0 rules:

- Require normal 32-bit file size.
- Require checksum type 0 or 15 only.
- Require destination filename length > 0.
- Permit source filename length = 0 only for metadata-only FDU later; for v0 file transfer, require source filename too.
- Reject options unless `CFDP_ALLOW_UNKNOWN_METADATA_TLVS` is set.
- For Class 1 closure mode, set closure requested = true.
- For acknowledged mode, closure flag is ignored by the standard, but encode zero for cleanliness.

Acceptance criteria:

- Encode/decode round-trip.
- Reject unsupported checksum type.
- Reject filenames longer than configured max.
- Reject truncated filename LV.
- Reject unexpected options unless configured to skip.

Suggested Codex prompt:

```text
Implement Metadata PDU encode/decode for the CFDP-lite subset. Use LV helpers for filenames. Support 32-bit file sizes only. Support checksum types 0 and 15 only. Reject unknown options for now. Add round-trip and malformed-PDU tests.
```

---

## Stage 5 — File Data encode/decode

Implement:

- `ccsds_cfdp_filedata_pdu_t`
- `ccsds_cfdp_encode_filedata()`
- `ccsds_cfdp_decode_filedata()`

Fields:

- Offset.
- Data pointer.
- Data length.

v0 rules:

- No segment metadata.
- Normal 32-bit offset.
- File data length may be zero only if explicitly allowed; otherwise reject zero-length File Data PDUs.
- Do not copy payload during decode.

Acceptance criteria:

- Encode/decode round-trip.
- Decode works for offset 0 and nonzero offsets.
- Reject PDU if segment metadata flag is set.
- Reject large-file mode until implemented.
- Reject truncated offset.

Suggested Codex prompt:

```text
Implement File Data PDU encode/decode for the CFDP-lite subset. No segment metadata, no large-file mode. Decode payload as pointer+length view. Add tests for offset handling and malformed input.
```

---

## Stage 6 — EOF encode/decode

Implement:

- `ccsds_cfdp_eof_pdu_t`
- `ccsds_cfdp_encode_eof()`
- `ccsds_cfdp_decode_eof()`

Fields:

- Condition code.
- File checksum.
- File size.
- Optional fault location TLV; reject/ignore initially depending config.

v0 rules:

- Normal EOF: condition code = no error.
- File size is total file-data octets.
- File checksum is 32-bit.
- Reject fault-location TLV initially unless adding minimal TLV skip logic.

Acceptance criteria:

- Encode/decode no-error EOF.
- Decode EOF with nonzero condition code.
- Reject truncated EOF.
- Reject unexpected fault location unless configured to skip.

Suggested Codex prompt:

```text
Implement EOF PDU encode/decode for normal 32-bit file-size mode. Include condition code, checksum, and file size. Reject optional fault-location TLV for now. Add unit tests.
```

---

## Stage 7 — Finished encode/decode

Implement:

- `ccsds_cfdp_finished_pdu_t`
- `ccsds_cfdp_encode_finished()`
- `ccsds_cfdp_decode_finished()`

Fields:

- Condition code.
- Delivery code: complete/incomplete.
- File status.
- Optional filestore response TLVs, omitted in v0.
- Optional fault location TLV, omitted in v0.

v0 rules:

- Success: no error + data complete + file retained.
- Failure: relevant condition code + data incomplete + status retained/discarded/status unreported.
- Reject filestore response TLVs initially.

Acceptance criteria:

- Encode/decode success Finished.
- Encode/decode failure Finished.
- Reject truncated Finished.
- Reject optional TLVs unless configured to skip.

Suggested Codex prompt:

```text
Implement Finished PDU encode/decode for the CFDP-lite subset. Support condition code, delivery code, and file status. Do not implement filestore response TLVs yet. Add unit tests.
```

---

## Stage 8 — ACK encode/decode, v1 only

Do not implement this before v0b Class 1 closure works. ACK exists for the
limited Class 2 milestone and should not add state or code size to the first
usable Class 1 path.

Implement:

- `ccsds_cfdp_ack_pdu_t`
- `ccsds_cfdp_encode_ack()`
- `ccsds_cfdp_decode_ack()`

v1 needs only:

- ACK of EOF.
- ACK of Finished.

Fields:

- Directive code being acknowledged.
- Directive subtype.
- Condition code.
- Transaction status.

v1 rules:

- Reject ACKs for anything other than EOF or Finished.
- Keep directive subtype constants narrow; document any assumed subtype values from the standard.
- For early v1, treat transaction status mainly as diagnostic.

Acceptance criteria:

- Encode/decode ACK EOF.
- Encode/decode ACK Finished.
- Reject ACK of unsupported directive.
- Reject malformed/truncated ACK.

Suggested Codex prompt:

```text
Implement ACK PDU encode/decode only for ACKing EOF and Finished directives. Follow src/connectivity/ccsds/CFDP_PLAN.md and keep unsupported ACK types rejected. Add tests.
```

---

## Stage 9 — NAK encode/decode, v1 only

Do not implement this before v0b Class 1 closure works. The range map can be
implemented earlier because Class 1 receive completeness needs it, but NAK PDU
encoding belongs with the later recovery milestone.

Implement:

- `ccsds_cfdp_nak_pdu_t`
- `ccsds_cfdp_nak_range_t`
- `ccsds_cfdp_encode_nak()`
- `ccsds_cfdp_decode_nak()`

Fields:

- Start of scope.
- End of scope.
- Segment requests, each with start offset and end offset.

v1 rules:

- Use normal 32-bit offsets only.
- End offset is exclusive.
- Number of segment requests must be <= `CCSDS_CFDP_MAX_NAK_RANGES`.
- For EOF-triggered recovery, use scope `[0, file_size)`.
- Reject invalid ranges where start >= end.
- Reject ranges outside scope.

Acceptance criteria:

- Encode/decode NAK with one range.
- Encode/decode NAK with multiple ranges.
- Reject too many ranges.
- Reject invalid ranges.
- Reject truncated NAK.

Suggested Codex prompt:

```text
Implement NAK PDU encode/decode with 32-bit scope and segment request offsets. Limit to CCSDS_CFDP_MAX_NAK_RANGES. Reject invalid or out-of-scope ranges. Add unit tests.
```

---

## Stage 10 — Modular checksum and null checksum

Implement:

- `ccsds_cfdp_checksum_init()`
- `ccsds_cfdp_checksum_update()`
- `ccsds_cfdp_checksum_finish()`
- Optional `ccsds_cfdp_checksum_reader()` helper for tests or file adapters.

Support:

- Modular checksum, checksum type 0.
- Null checksum, checksum type 15.

Rules:

- Keep the implementation streaming-friendly.
- Do not require the full file in RAM.
- Sender should compute the checksum while reading and emitting File Data, then
  put the final value in EOF. Do not require a pre-send full-file checksum pass.
- A file checksum helper may exist for tests, but the production sender should
  not depend on rewind or a second source-file read.
- Add a known test vector from a hand-computed small byte array.

Acceptance criteria:

- Modular checksum deterministic tests.
- Null checksum always zero.
- Unsupported checksum type returns `CFDP_ERR_UNSUPPORTED_CHECKSUM`.

Suggested Codex prompt:

```text
Implement CCSDS CFDP checksum support for modular checksum type 0 and null checksum type 15. Keep it streaming-friendly so the sender can update the checksum while emitting File Data. Add deterministic tests. Do not implement other checksum algorithms yet.
```

---

## Stage 11 — Range tracking

Implement:

- `ccsds_cfdp_ranges_init()`
- `ccsds_cfdp_ranges_add(start, end)`
- `ccsds_cfdp_ranges_is_complete(0, file_size)`
- `ccsds_cfdp_ranges_missing(0, file_size, out_ranges)`

Rules:

- Store a small sorted, merged interval list.
- End offsets are exclusive.
- Adding overlapping/adjacent ranges should merge them.
- Cap internal range count with `CCSDS_CFDP_MAX_NAK_RANGES` or a separate
  `CONFIG_AKIRA_CCSDS_CFDP_MAX_RX_RANGES` if Class 1 receive needs more slots.
- If internal range count overflows, return a clean error.

Acceptance criteria:

- Add in-order ranges.
- Add out-of-order ranges.
- Add overlapping ranges.
- Add adjacent ranges.
- Compute missing ranges.
- Test overflow behaviour.

Suggested Codex prompt:

```text
Implement a small fixed-capacity received range map for CCSDS CFDP receive completeness and later recovery. Use exclusive end offsets. Merge overlapping and adjacent ranges. Add tests for missing-range computation after EOF.
```

---

## Stage 12 — File callback API

Implement `ccsds_cfdp_filestore.h` only first.

Callbacks:

```c
typedef struct {
    void *user;
    int (*open_read)(void *user, const char *path, void **handle, uint32_t *size);
    int (*open_write_tmp)(void *user, const char *dst_path, void **handle);
    int (*read)(void *user, void *handle, uint32_t offset, uint8_t *buf, size_t len, size_t *nread);
    int (*write)(void *user, void *handle, uint32_t offset, const uint8_t *buf, size_t len);
    int (*close)(void *user, void *handle);
    int (*commit_tmp)(void *user, const char *dst_path);
    int (*discard_tmp)(void *user, const char *dst_path);
} ccsds_cfdp_filestore_ops_t;
```

Rules:

- Random-access writes are required for recovery.
- `commit_tmp` should be atomic when the OS/FS supports it.
- Do not put WASM activation here.
- Keep this as a thin callback boundary. Do not add path policy, manifests,
  activation, or a virtual filesystem layer here.
- For tests, create a small in-memory filestore implementation in test code, not
  as production source.

Acceptance criteria:

- Header compiles.
- In-memory test filestore supports read/write/commit/discard.
- No receiver/sender state machine yet.

Suggested Codex prompt:

```text
Define the CCSDS CFDP filestore callback interface and add an in-memory filestore test implementation under tests. Do not implement sender or receiver logic yet.
```

---

## Stage 13 — Unitdata Transfer callback API

Implement the minimum send/receive callback boundary in `ccsds_cfdp_entity.h`.
Only split it into `ccsds_cfdp_ut.h` if the entity header becomes noisy.

Callbacks:

```c
typedef struct {
    void *user;
    int (*send_pdu)(void *user,
                    uint64_t dest_entity_id,
                    const uint8_t *pdu,
                    size_t pdu_len);
    uint64_t (*now_ms)(void *user);
} ccsds_cfdp_ut_ops_t;
```

Add loopback test UT:

- Sender side can inject PDUs directly into receiver.
- Test UT can optionally drop selected File Data PDUs later, but keep that in
  test code until v1 recovery work starts.

Acceptance criteria:

- Loopback UT can deliver a raw PDU to a registered receiver callback.
- Optional drop rule by directive type or file-data offset exists for tests.

Suggested Codex prompt:

```text
Create a small CCSDS CFDP Unitdata Transfer callback boundary and an in-memory loopback UT for tests. It should send opaque CFDP PDUs between test entities. Keep drop/reorder rules in tests unless v1 recovery is being implemented.
```

---

## Stage 14 — Entity object and transaction IDs

Implement:

- `ccsds_cfdp_entity_t`
- `ccsds_cfdp_transaction_id_t`
- One sender transaction slot.
- One receiver transaction slot.
- Transaction sequence number allocator.

Rules:

- Transaction ID = source entity ID + transaction sequence number.
- Fixed remote entity config for now.
- No dynamic MIB.
- No multi-peer routing yet.
- Entity storage owns all transaction state and fixed buffers. Do not allocate
  transaction state dynamically.
- Provide an event callback in this stage or the next one so cleanup is testable
  before Class 1 closure is added.

Acceptance criteria:

- Create/init entity.
- Allocate transaction ID.
- Match incoming PDU to receiver transaction.
- Reject unexpected second simultaneous transaction cleanly.

Suggested Codex prompt:

```text
Implement the CCSDS CFDP entity skeleton with one sender slot and one receiver slot. Include transaction ID allocation, matching, fixed caller-owned storage, and a small event callback. No file transfer state machine yet.
```

---

## Stage 15 — Send Metadata, File Data, EOF

Implement `ccsds_cfdp_send_file_class1()`.

Flow:

1. Open source file.
2. Send Metadata PDU with closure requested.
3. Read file in fixed-size chunks.
4. Update the modular checksum for each chunk.
5. Send File Data PDU for each chunk.
6. Send EOF PDU with final checksum and file size.
7. If closure requested, wait for Finished.

Rules:

- Do not require a pre-send checksum pass or source-file rewind.
- Keep one segment buffer in the caller/entity state. Do not allocate one per
  PDU.
- If the filestore read fails mid-transfer, send EOF with an appropriate fault
  condition if possible, then fail the local transaction.

Acceptance criteria:

- Loopback captures Metadata, all File Data, and EOF in correct transaction.
- Offsets increase correctly.
- EOF file size and streaming checksum match source file.
- No receiver implementation required yet; test can inspect emitted PDUs.

Suggested Codex prompt:

```text
Implement a simple Class 1 CCSDS CFDP sender that emits Metadata, File Data PDUs, and EOF over the UT callback. Use fixed segment size, 32-bit file sizes, and streaming checksum while reading. Add tests using the loopback/capture UT. Do not implement Finished waiting yet.
```

---

## Stage 16 — Receive Metadata, File Data, EOF

Implement incoming PDU dispatch:

- Decode header.
- Dispatch Metadata/FileData/EOF.
- Create receiver transaction on Metadata.
- Open temp destination file.
- Write File Data at offset.
- Mark received ranges.
- On EOF, check completeness and checksum.

Rules:

- Do not assume File Data arrives after Metadata; for v0, reject File Data that
  arrives before Metadata and count it as a fault. Do not add pending segment
  storage until a real transport requires it.
- If EOF arrives with missing ranges, report incomplete.
- If checksum fails, report checksum failure.
- Verify checksum by streaming over the received file or by updating a receive
  checksum only when in-order writes are guaranteed. The generic v0 receiver
  should not trust arrival order for checksum calculation.

Acceptance criteria:

- Complete transfer over loopback produces destination file.
- Out-of-order File Data writes still produce correct file if Metadata arrived first.
- Missing File Data produces incomplete status.
- Bad checksum produces failure status.

Suggested Codex prompt:

```text
Implement the CCSDS CFDP Class 1 receiver for Metadata, File Data, and EOF. Use the range map and filestore callbacks. On EOF verify completeness and checksum. Do not send Finished yet. Add loopback tests.
```

---

## Stage 17 — Finished PDU and sender completion

Receiver:

- After EOF and verification, send Finished PDU if closure requested.
- Finished success if data complete and checksum OK.
- Finished failure otherwise.

Sender:

- After EOF, wait for Finished.
- On Finished success, report complete.
- On Finished failure, report failed.
- Add a simple timeout.

Acceptance criteria:

- Complete loopback transfer ends with sender success.
- Receiver sends Finished success.
- Dropped File Data leads to Finished failure in Class 1.
- Sender observes failure.

Suggested Codex prompt:

```text
Add Class 1 transaction closure. Receiver should send Finished after EOF when closure is requested. Sender should wait for Finished and report success/failure. Add tests for complete and missing-data transfers.
```

---

## Stage 18 — Staging paths only

Add an Akira-facing wrapper, not WASM activation yet:

```c
int akira_cfdp_receive_to_staging(const char *dst_name);
int akira_cfdp_commit_staged(const char *dst_name);
int akira_cfdp_discard_staged(const char *dst_name);
```

Rules:

- Destination path must be constrained to a staging directory.
- Reject `..`, absolute paths, and illegal characters.
- CFDP destination filename is a logical name, not arbitrary filesystem authority.
- Do not run the WASM file yet.

Acceptance criteria:

- Transfer to staging directory.
- Commit to final path only after success.
- Reject path traversal.

Suggested Codex prompt:

```text
Add a thin AkiraOS staging wrapper around the CFDP receiver. Destination filenames must be sanitized and constrained to a staging directory. Do not implement WASM activation or signature verification.
```

---

## Stage 19 — Carry CFDP PDUs over Space Packets

Implement after loopback Class 1 is solid and before Class 2 recovery.

Recommended mapping:

- One CFDP PDU per CCSDS Space Packet initially.
- Dedicated APID for CFDP, or one APID per direction/service.
- Space Packet payload = entire CFDP PDU.
- No CFDP PDU fragmentation initially.
- Constrain `CCSDS_CFDP_MAX_SEGMENT_SIZE` so the encoded CFDP PDU fits inside
  the configured CCSDS Space Packet payload limit.

Rules:

- Keep CFDP unaware of APID/VC/MAP.
- UT adapter maps remote entity ID to CCSDS route.
- Register the receive path through the existing CCSDS APID router.
- If using COP-1 on uplink, still keep CFDP Class 1 closure available. Do not
  add Class 2 just because COP-1 exists.

Acceptance criteria:

- Loopback Space Packet adapter can transfer a file.
- Malformed Space Packet payload is rejected before CFDP dispatch.
- CFDP PDU size never exceeds configured Space Packet payload limit.

Suggested Codex prompt:

```text
Implement a CCSDS Space Packet UT adapter for CFDP where each Space Packet payload contains exactly one CFDP PDU. Use the existing CCSDS Space Packet codec and APID router. Keep route/APID logic outside the CFDP engine. Add tests using a loopback Space Packet encoder/decoder.
```

---

## Stage 20 — Generate NAK after EOF

Receiver changes for acknowledged mode:

1. Receive EOF.
2. Send ACK EOF.
3. If missing ranges exist, send NAK with scope `[0, file_size)`.
4. Enter recovery state.
5. Keep accepting retransmitted File Data.
6. When complete, verify checksum and send Finished.

Rules:

- NAK only after EOF initially.
- No immediate NAK on gap detection.
- If too many missing ranges, send NAK for first N ranges or fail with NAK-limit fault.
- Add `nak_retry_count` but do not implement retry until next stage.

Acceptance criteria:

- If one File Data PDU is dropped, receiver sends NAK for its exact byte range after EOF.
- If multiple chunks are dropped, NAK contains multiple ranges.
- Receiver does not send Finished until missing data arrives.

Suggested Codex prompt:

```text
Add EOF-triggered NAK generation to the receiver for acknowledged-mode transfers. On EOF, ACK EOF, compute missing ranges, and send a NAK. Do not implement sender retransmission yet. Add tests that drop selected File Data PDUs and inspect the NAK.
```

---

## Stage 21 — Handle NAK and resend ranges

Sender changes:

- Keep source file available until transaction completes.
- On NAK, validate scope and requested ranges.
- For each requested range, send File Data PDUs covering that range.
- After retransmission, resend EOF or wait depending chosen policy.

Recommended simple policy:

- Resend EOF after satisfying a NAK. This helps the receiver re-trigger verification without extra state assumptions.

Rules:

- Reject NAK ranges outside file size.
- Split retransmission into normal segment-size File Data PDUs.
- Count NAK rounds; fail after `CONFIG_AKIRA_CCSDS_CFDP_MAX_NAK_ROUNDS`.

Acceptance criteria:

- Drop one chunk in loopback; receiver NAKs; sender retransmits; receiver completes; Finished success.
- Drop multiple chunks; recovery succeeds.
- Invalid NAK fails transaction cleanly.

Suggested Codex prompt:

```text
Implement sender-side NAK handling for limited Class 2. On a valid NAK, resend the requested file byte ranges as File Data PDUs and then resend EOF. Keep the source file open or reopenable until Finished. Add loopback tests with dropped chunks.
```

---

## Stage 22 — ACK Finished and complete transaction

Receiver:

- After sending Finished, wait for ACK Finished.
- Complete transaction when ACK Finished arrives.

Sender:

- On Finished, send ACK Finished.
- Complete based on Finished status.

Acceptance criteria:

- Class 2 happy path includes EOF ACK and Finished ACK.
- Receiver transaction is released after ACK Finished.
- Sender transaction is released after sending ACK Finished.

Suggested Codex prompt:

```text
Add ACK handling for Finished in the limited Class 2 flow. Sender should ACK Finished. Receiver should release the transaction after receiving ACK Finished. Add tests for transaction cleanup.
```

---

## Stage 23 — Conservative timeout handling

Implement polling:

```c
void ccsds_cfdp_entity_poll(ccsds_cfdp_entity_t *entity, uint64_t now_ms);
```

Timers:

- Sender waiting for Finished after EOF.
- Sender waiting during Class 2 recovery.
- Receiver waiting for retransmitted data after NAK.
- Receiver waiting for ACK Finished.
- General inactivity monitor.

Retry policy:

- Class 1 closure: no recovery; timeout gives unknown/failure.
- Class 2:
  - Receiver may resend NAK on recovery timeout.
  - Receiver may resend Finished until ACK Finished or limit.
  - Sender may resend EOF on response timeout.

Acceptance criteria:

- Lost Finished is retried in Class 2.
- Lost NAK is retried by receiver after timeout.
- Transaction fails cleanly after retry limit.

Suggested Codex prompt:

```text
Add ccsds_cfdp_entity_poll() and simple retry timers for limited Class 2. Receiver should retry NAK and Finished; sender should retry EOF while waiting. Fail cleanly after configured limits. Add deterministic tests using fake now_ms.
```

---

## Stage 24 — Clean errors and events

Add user events:

```c
typedef enum {
    CCSDS_CFDP_EVENT_TRANSACTION_STARTED,
    CCSDS_CFDP_EVENT_METADATA_RECV,
    CCSDS_CFDP_EVENT_FILE_SEGMENT_RECV,
    CCSDS_CFDP_EVENT_EOF_RECV,
    CCSDS_CFDP_EVENT_NAK_SENT,
    CCSDS_CFDP_EVENT_NAK_RECV,
    CCSDS_CFDP_EVENT_RETRANSMIT,
    CCSDS_CFDP_EVENT_FINISHED_SENT,
    CCSDS_CFDP_EVENT_FINISHED_RECV,
    CCSDS_CFDP_EVENT_COMPLETE,
    CCSDS_CFDP_EVENT_FAILED
} ccsds_cfdp_event_type_t;
```

Minimum faults:

- Bad PDU length.
- Unsupported version.
- Unsupported checksum type.
- Unsupported large-file flag.
- Unsupported segment metadata.
- File too large.
- Duplicate transaction.
- Metadata missing.
- File write failure.
- File checksum failure.
- File size error.
- NAK limit reached.
- Inactivity detected.
- Cancelled.

Acceptance criteria:

- Every failure path emits one final failure event.
- No transaction slot leak after failure.
- Tests cover representative faults.

Suggested Codex prompt:

```text
Add CCSDS CFDP event reporting and consolidate failure cleanup. Ensure every transaction ends in exactly one COMPLETE or FAILED event and frees its transaction slot. Add tests for bad PDU, checksum failure, and timeout.
```

---

## Stage 25 — Instantiate a file-agnostic AkiraOS CFDP service

Create a concrete AkiraOS service that wires the CFDP engine into the CCSDS
stack, without adding any artifact-specific behavior.

The service owns one static CFDP entity and one Space Packet adapter. It binds:

- Local and remote CFDP entity IDs.
- CFDP Space Packet APID and outbound packet type.
- CCSDS router receive registration.
- UT send path through the Space Packet adapter.
- Receive filestore/staging callbacks.
- CFDP event callback.

Rules:

- Keep the service generic: transfer arbitrary named files only.
- Do not add WASM, OTA, signature, activation, manifest, or installer policy.
- Do not add CFDP-owned background threads.
- Do not poll IO interfaces from `ccsds_cfdp_entity_poll()`.
- Keep transport-specific code outside the CFDP entity.
- Use static storage and Kconfig-backed defaults.
- Default to one symmetric CFDP APID for peer-to-peer test deployments.
- Packet type is an outbound Space Packet policy only. Inbound CFDP packets are
  accepted or rejected by APID, Space Packet validity, and CFDP PDU/entity
  checks, not by whether the Space Packet is marked TM or TC.

Acceptance criteria:

- Application code can call one service init function and register CFDP RX.
- A CFDP PDU arriving on the configured APID reaches
  `ccsds_cfdp_entity_receive_pdu()`.
- Outbound CFDP PDUs are wrapped in CCSDS Space Packets using the adapter.
- The service can receive an arbitrary file into staging in a loopback or test
  fixture.
- The service exposes terminal COMPLETE/FAILED events through a callback.

Suggested Codex prompt:

```text
Add a generic AkiraOS CFDP service instance that wires the existing CFDP entity to the Space Packet adapter and CCSDS router. Keep it file-agnostic: no WASM, installer, signatures, or activation. Use static storage, Kconfig-backed IDs/APID defaults, and focused tests that prove APID dispatch reaches the CFDP entity and outbound PDUs use the Space Packet adapter.
```

---

## Stage 26 — Route bounded input units at the configured CCSDS layer

Create one central CCSDS input boundary used by UDP and later UART/RF
transports. The transport supplies one bounded input unit; the CCSDS stack
decides how to interpret that unit from compile-time profile support.

Rules:

- If `CONFIG_AKIRA_CCSDS_FRAME_SUPPORT=y`, a bounded input unit is a complete
  TC CLTU and uses the existing CLTU/TC profile path.
- If `CONFIG_AKIRA_CCSDS_FRAME_SUPPORT=n`, a bounded input unit is one encoded
  CCSDS Space Packet and is decoded then dispatched by APID.
- Keep this central. Do not grow separate transport modules whose names bake in
  both transport and CCSDS layer, such as one UDP module for CLTU and another
  unrelated UDP module for packets.
- Do not auto-detect CLTU versus Space Packet from bytes.
- Preserve the APID router as the handoff point to packet services such as CFDP.

Acceptance criteria:

- A packet-only build can pass an encoded Space Packet into the central input
  function and reach the registered APID handler.
- A frame-support build still passes complete CLTUs through the existing TC
  receive profile.
- Malformed packet-level input fails before APID dispatch.
- Existing TC/CLTU and Space Packet tests continue to pass.

Suggested Codex prompt:

```text
Add a central CCSDS input-unit dispatch boundary. When CONFIG_AKIRA_CCSDS_FRAME_SUPPORT is enabled, preserve the existing complete-CLTU TC receive path. When it is disabled, treat each input unit as one encoded CCSDS Space Packet and dispatch it by APID. Keep this transport-neutral and add focused tests for packet-level dispatch.
```

---

## Stage 27 — Make UDP use the central input path

Use UDP as the first byte-unit transport for packet-only CFDP integration, but
do not make UDP inherently CLTU-only or packet-only. UDP receives and sends
bounded CCSDS units; the configured build/layer determines whether those units
are packets or CLTUs/frames.

Near-term packet-only profile:

```text
CONFIG_AKIRA_CCSDS=y
CONFIG_AKIRA_CCSDS_CFDP=y
CONFIG_AKIRA_CCSDS_FRAME_SUPPORT=n
CONFIG_NETWORKING=y
```

Rules:

- In packet-only builds, each inbound UDP datagram is one encoded Space Packet.
- In frame-support builds, existing complete-CLTU UDP behavior is preserved
  until explicit runtime layer selection is added.
- Outbound packet-level sends use the encoded Space Packet emitted by the CFDP
  Space Packet adapter and send it to the configured UDP peer.
- Use distinct local and peer UDP addresses/ports for the two instances, but do
  not create duplicate CFDP-specific transport logic.
- Keep transport-specific code outside the CFDP entity.

Acceptance criteria:

- A packet-only build can receive a UDP datagram containing one Space Packet and
  dispatch it to the CFDP APID handler.
- The CFDP service can send an outbound Space Packet through the UDP endpoint to
  a configured peer.
- UDP transport statistics expose datagrams received/sent and last error.
- Existing CLTU UDP behavior remains available in frame-support builds.

Suggested Codex prompt:

```text
Refactor the CCSDS UDP input/output path to use the central CCSDS input-unit dispatch boundary. In packet-only builds, UDP datagrams are encoded CCSDS Space Packets; in frame-support builds, preserve existing complete-CLTU behavior. Wire outbound encoded Space Packets from the CFDP adapter to the configured UDP peer without adding CFDP-specific transport logic.
```

---

## Stage 28 — Configure and observe the generic CFDP service

Add shell/runtime control for the file-agnostic CFDP service so two identical
AkiraOS instances can be configured as peers without rebuilding.

Minimum shell surface:

```text
ccsds cfdp config local <entity-id> remote <entity-id> apid <apid>
ccsds cfdp start
ccsds cfdp put <source-path> <destination-name>
ccsds cfdp status
```

Supported reporting targets can be minimal at first:

- Zephyr logs for bring-up.
- Shell/queryable status.
- A small AkiraOS/CCSDS TM status packet later if a flight-facing path is ready.

Rules:

- The CFDP entity remains callback-only for local events.
- The service-level adapter translates CFDP events into logs/status or TM.
- Do not encode WASM/application semantics in CFDP status.
- Do not make logs the only long-term observability mechanism if TM status is
  required later.
- Do not require operator-supplied checksums. CFDP EOF checksum verification is
  the protocol-level integrity check.
- Entity IDs and APID must be runtime-overridable before service start.

Acceptance criteria:

- COMPLETE and FAILED events from the service are visible outside unit tests.
- Reports include transaction ID, event type, CFDP status, source/destination
  filenames, file size, checksum value, and checksum status `OK`/`NOK`.
- Timeout/retry-limit failures are distinguishable from checksum and filestore
  failures.
- Reporting can be disabled or stubbed in tests.
- `ccsds cfdp status` can report, for example:

```text
last transaction=1:7 source=test.bin dest=test.bin size=4096 checksum=0x8a12f034 checksum=OK status=COMPLETE
```

Suggested Codex prompt:

```text
Add runtime/shell control and concrete observability for the generic AkiraOS CFDP service. Allow local/remote entity IDs and APID to be configured before start, add an arbitrary-file put command, and expose terminal status including transaction ID, file names, size, checksum value, checksum OK/NOK, and COMPLETE/FAILED. Keep the service file-agnostic.
```

---

## Stage 29 — Exercise both ends over packet-level UDP

Use the same CFDP implementation as both peers so AkiraOS can test a real
service instance against another instance before CLTU generation and TM frame
parsing are available.

Recommended first integration profile: UDP carrying one encoded Space Packet
per datagram with `CONFIG_AKIRA_CCSDS_FRAME_SUPPORT=n`. This is a CFDP
integration transport, not a full TC/TM flight-link validation.

Flow:

1. Instance A runs the generic CFDP service with local entity `1`, remote
   entity `2`, and CFDP APID `0x340`.
2. Instance B runs the same service with local entity `2`, remote entity `1`,
   and the same CFDP APID `0x340`.
3. Each instance binds a UDP endpoint and configures the other endpoint as its
   peer.
4. Instance A sends an arbitrary file to Instance B.
5. Instance B reports received file, size, checksum `OK`/`NOK`, and terminal
   COMPLETE/FAILED status.
6. Reverse direction can be added after one direction is proven.

Rules:

- Keep this as a test/harness, not artifact install policy.
- Use arbitrary binary files, not WASM-specific fixtures only.
- Preserve asynchronous receive through `ccsds_cfdp_entity_receive_pdu()`.
- Keep `ccsds_cfdp_entity_poll()` for protocol timeout/retry maintenance only.
- The receiver reports COMPLETE only after CFDP verifies received ranges, EOF
  file size, and EOF checksum.
- Do not require or expose an operator-supplied checksum as part of the CFDP
  service contract.
- Full CLTU/TM frame end-to-end testing remains a later ground-link emulator
  milestone.

Acceptance criteria:

- Native/native or native/board peer can transfer a small arbitrary file to the
  receiving service.
- The receiver observes transaction COMPLETE or FAILED.
- Packet exchange goes through the configured Space Packet APID path.
- Receiver status shows destination filename, received size, EOF checksum, and
  checksum `OK` for an intact transfer.
- Corrupted or missing data produces FAILED with checksum or incomplete-file
  failure, not COMPLETE.
- A dropped segment in acknowledged mode exercises NAK recovery end to end.
- Failures are visible through generic CFDP service observability.

Suggested Codex prompt:

```text
Add a two-instance CFDP deployment test that uses the same AkiraOS CFDP service on both peers. In a packet-only build with CONFIG_AKIRA_CCSDS_FRAME_SUPPORT disabled, carry one encoded Space Packet per UDP datagram through the configured APID path. Transfer an arbitrary file, verify that the receiver reports file size and checksum OK from CFDP EOF verification, and keep WASM/installer behavior out of the harness.
```

---

## Stage 30 — Validate CFDP over the realistic asymmetric CCSDS link path

After packet-level CFDP deployment works, add a separate ground-test entity or
host tool for the full link path:

- Ground to board: CFDP PDU -> Space Packet -> TC frame/segment -> CLTU.
- Board to ground: CFDP PDU -> Space Packet -> TM transfer frame/CADU.

This stage is needed for flight-link confidence, but it must not block the
earlier packet-level CFDP deployment test.

Acceptance criteria:

- Host tool can generate CLTUs carrying CFDP Space Packets for board ingest.
- Host tool can parse TM frames/CADUs, extract Space Packets, and dispatch the
  CFDP APID.
- The same generic CFDP service runs unchanged above the link path.
- File arrival status still reports size and checksum `OK`/`NOK`.

---

## Stage 31 — Above CFDP, not inside CFDP

After CFDP transfer works, add artifact semantics:

- Manifest file.
- WASM file.
- Optional signature file.
- Version.
- Runtime limits.
- Required ABI/API version.
- Hash of WASM payload.

Flow:

1. CFDP receives file into staging.
2. Installer validates path and manifest.
3. Installer checks hash/signature.
4. Installer checks runtime compatibility.
5. Installer atomically activates.
6. Installer reports status over MQTT/NATS/CCSDS pub-sub channel.

Acceptance criteria:

- CFDP can deliver arbitrary file to staging.
- Installer can reject invalid artifact without involving CFDP.
- Installer can commit known-good artifact.
- Rollback path exists.

Suggested Codex prompt:

```text
Add an AkiraOS WASM artifact installer above CFDP. CFDP should only deliver files to staging. The installer should validate manifest/hash and atomically activate or reject the artifact. Do not modify CFDP protocol logic.
```

---

## Test Scenarios

### Must pass before calling v0 complete

- Small text file transfer, Class 1 closure, no loss.
- Binary WASM-like file transfer, Class 1 closure, no loss.
- Empty file, if supported.
- File size exactly one segment.
- File size one byte over one segment.
- File size multiple segments.
- Out-of-order File Data after Metadata.
- Duplicate File Data.
- Dropped File Data in Class 1 produces Finished failure.
- Bad checksum produces Finished failure.
- Destination path traversal rejected.

### Must pass before calling v1 complete

- One dropped segment recovered by NAK.
- Multiple dropped segments recovered by NAK.
- Dropped EOF recovered by sender retry.
- Dropped NAK recovered by receiver retry.
- Dropped Finished recovered by receiver retry.
- Too many missing ranges fails cleanly.
- Invalid NAK range fails cleanly.
- Timeout/inactivity fails cleanly.

---

### Must pass before calling packet-level deployment usable

- Two instances can run with different local/remote entity IDs and the same
  CFDP APID.
- Packet-only build with `CONFIG_AKIRA_CCSDS_FRAME_SUPPORT=n` treats each UDP
  input unit as one encoded Space Packet.
- A small arbitrary file transfers over UDP between two instances.
- Receiver status reports destination filename, file size, EOF checksum, and
  checksum `OK`.
- Corrupt or incomplete transfer reports checksum `NOK` or incomplete-file
  FAILED status.

### Must pass before calling full link E2E usable

- Host tool can generate CLTUs carrying CFDP Space Packets for board ingest.
- Host tool can parse TM frames/CADUs and extract CFDP Space Packets.
- The same CFDP service and APID path work above both packet-level UDP and the
  CLTU/TM-frame path.

---

## Session-by-Session Checklist

Use this as a literal work queue.

```text
[ ] v0a-01 Kconfig, constants, enums, status
[ ] v0a-02 fixed header codec
[ ] v0a-03 LV/TLV helpers
[ ] v0a-04 Metadata PDU codec
[ ] v0a-05 File Data PDU codec
[ ] v0a-06 EOF PDU codec
[ ] v0a-07 Finished PDU codec
[ ] v0a-08 checksum module
[ ] v0b-09 range map
[ ] v0b-10 filestore callback boundary + in-memory test filestore
[ ] v0b-11 send/receive callback boundary + loopback test UT
[ ] v0b-12 entity skeleton + transaction IDs + event callback
[ ] v0b-13 Class 1 sender emits Metadata/FileData/EOF with streaming checksum
[ ] v0b-14 Class 1 receiver writes file and verifies EOF
[ ] v0b-15 Class 1 closure with Finished and simple timeout
[ ] v0b-16 Akira staging wrapper
[ ] v0c-17 CCSDS Space Packet APID adapter
[ ] v1-18 ACK PDU codec
[ ] v1-19 NAK PDU codec
[ ] v1-20 Class 2 receiver EOF-triggered NAK
[ ] v1-21 Class 2 sender NAK retransmission
[ ] v1-22 Finished ACK handling
[ ] v1-23 timers/retries/poll
[ ] v1-24 fault/event expansion for recovery paths
[ ] svc-25 generic AkiraOS CFDP service instance
[ ] io-26 central CCSDS input-unit dispatch
[ ] io-27 UDP endpoint uses central input/output path
[ ] svc-28 CFDP runtime control and checksum/status reporting
[ ] e2e-29 two-instance packet-level CFDP deployment test
[ ] e2e-30 ground-link emulator for CLTU/TM-frame E2E
[ ] app-31 WASM artifact installer above CFDP
```

---

## Guardrails for Codex Sessions

At the start of each Codex session, paste the relevant stage and this instruction:

```text
Work only on the named stage. Do not implement later stages. Keep the code compiling and add focused tests. Avoid heap allocation in codec code. Do not add proxy operations, filestore request TLVs, directory operations, BP/DTN, IP, HTTP, or WASM activation unless this stage explicitly asks for them.
```

At the end of each session, ask Codex:

```text
Summarize exactly what changed, what tests were added, what remains unimplemented, and whether any behavior deviates from src/connectivity/ccsds/CFDP_PLAN.md.
```

---

## Suggested Final Architecture Diagram

```text
                MQTT/NATS-like pub-sub over CCSDS
                   commands, status, progress
                             |
                             v
+-------------------------------------------------------------+
|                     Akira application layer                 |
|  install request | status | verify | activate | rollback    |
+-----------------------------+-------------------------------+
                              |
                              v
+-------------------------------------------------------------+
|                         CFDP-lite                           |
|  Metadata | FileData | EOF | Finished | ACK | NAK           |
|  Class 1 closure -> limited Class 2 missing-range recovery  |
+-----------------------------+-------------------------------+
                              |
                              v
+-------------------------------------------------------------+
|                        UT adapter                           |
|  loopback | packet UDP | CCSDS Space Packet | TC/TM link    |
+-----------------------------+-------------------------------+
                              |
                              v
+-------------------------------------------------------------+
|              CCSDS link layer / COP-1 / SDLS / coding       |
+-------------------------------------------------------------+
```

---

## Definition of Done

### v0a done

- CFDP fixed header codec passes malformed and round-trip tests.
- Metadata, File Data, EOF, and Finished codec tests pass.
- Modular checksum and null checksum tests pass.
- No ACK, NAK, Class 2, file staging, or Space Packet adapter code is compiled
  into the v0a milestone.

### v0b done

- Class 1 with closure transfers a file over loopback UT.
- Receiver writes to staging and verifies checksum.
- Sender receives Finished success/failure.
- Dropped File Data causes a clean failure, not corruption.
- Sender computes checksum while sending, without a full pre-send file pass.
- All codec tests pass.
- No deferred features accidentally implemented.

### v0c done

- A dedicated CCSDS APID can carry one CFDP PDU per Space Packet.
- Segment-size limits guarantee encoded CFDP PDUs fit the configured Space
  Packet payload limit.
- APID adapter tests pass without adding CFDP-level fragmentation.

### v1 done

- Limited Class 2 recovers EOF-detected missing ranges using NAK.
- Sender retransmits requested ranges only.
- EOF ACK and Finished ACK exist.
- Timers and retry limits exist.
- Recovery tests pass with deliberately dropped PDUs.

### Packet-level deployment done

- Packet-only build with `CONFIG_AKIRA_CCSDS_FRAME_SUPPORT=n` routes bounded
  input units as encoded Space Packets through the APID router.
- UDP can carry one encoded Space Packet per datagram between two AkiraOS
  instances.
- Two instances can be configured at runtime with different local/remote CFDP
  entity IDs and the same CFDP APID.
- An arbitrary file transfers between the two instances using the generic CFDP
  service.
- Receiver status reports destination filename, file size, EOF checksum value,
  checksum `OK`, and COMPLETE for an intact transfer.
- Corrupt or incomplete transfer reports FAILED with checksum `NOK` or an
  incomplete-file cause.

### Full link E2E done

- A host/ground test entity can generate CLTUs carrying CFDP Space Packets for
  board ingest.
- A host/ground test entity can parse TM frames/CADUs, extract Space Packets,
  and dispatch the CFDP APID.
- The generic CFDP service runs unchanged above the full asymmetric link path.

### Akira demo done

- WASM-like binary delivered over CFDP to staging.
- Installer validates and commits it separately.
- Progress/status can be reported over your pub-sub channel.
- HTTP remains optional developer convenience, not the core transfer architecture.

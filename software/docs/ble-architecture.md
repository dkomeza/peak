# BLE architecture and OTA protocol

## Scope

The ESP32-P4 application owns one NimBLE host connected to the ESP32-C6 controller
through ESP-Hosted. BLE initially exposes two independent services:

- Nordic UART Service (NUS), preserving the UUIDs and raw byte stream expected by
  VESC Tool.
- PEAK OTA v1, accepting a firmware image directly over BLE without Wi-Fi or an
  HTTP server.

A future PEAK mobile-app service should be added as a separate GATT service. App
messages must not be multiplexed into the NUS stream or the OTA protocol.

The BLE host owns GAP state, advertising, connection/subscription state, the static
GATT table, and serialized outgoing notifications/indications. GATT callbacks do
only bounded validation and queue copies. Dedicated workers own VESC/CAN processing
and flash writes. The OTA session component owns the inactive OTA partition and has
no dependency on BLE, HTTP, JSON, or Wi-Fi.

## BLE v1 constraints

- One connected central at a time.
- Peripheral/GATT-server roles only.
- One sequential OTA session at a time.
- No transfer resume in v1; a disconnect aborts the session and the next attempt
  starts at offset zero.
- Direct OTA `BEGIN` is accepted only in `BOOT_MODE_CONFIG`, entered by holding
  POWER+DOWN through the initial boot hold. Normal mode still exposes BLE and NUS,
  but rejects `BEGIN`.
- OTA data uses GATT Write With Response for simple delivery backpressure.
- All multibyte integers are little-endian.
- Packets have fixed headers and are decoded field-by-field; C structure layout is
  never used as an on-air format.
- The existing firmware remains selected until the complete new image is verified.

NUS and OTA may be advertised together. The NUS UUID remains in the primary
advertisement for VESC Tool discovery; the OTA UUID and complete unique device name
(for example, `PEAK-A1B2`) use the scan response. The same name is exposed through
the mandatory GAP Device Name characteristic. Its suffix is derived from the hosted
C6 controller's Bluetooth MAC address, not the P4's unrelated base MAC.

## PEAK OTA v1 GATT service

| Item | UUID | Properties |
| --- | --- | --- |
| Service | `a6ed0701-d344-460a-8075-b9e8ec90d71b` | Primary service |
| Control | `a6ed0702-d344-460a-8075-b9e8ec90d71b` | Write With Response |
| Data | `a6ed0703-d344-460a-8075-b9e8ec90d71b` | Write With Response |
| Status | `a6ed0704-d344-460a-8075-b9e8ec90d71b` | Read, Notify |

The client subscribes to Status before issuing `BEGIN`.

### Control packets

`BEGIN` is exactly 38 bytes:

| Offset | Size | Field | Value |
| ---: | ---: | --- | --- |
| 0 | 1 | command | `0x01` |
| 1 | 1 | protocol version | `0x01` |
| 2 | 4 | image size | Unsigned little-endian byte count |
| 6 | 32 | SHA-256 | Digest of the complete firmware file |

The remaining commands are exactly one byte:

| Command | Byte | Meaning |
| --- | ---: | --- |
| `FINISH` | `0x02` | End input and verify the complete image |
| `ABORT` | `0x03` | Abort the current session |
| `QUERY` | `0x04` | Request/currently read the latest status |

Unknown commands, incorrect packet lengths, unsupported versions, and `BEGIN` while
a session is active are protocol errors.

### Data packets

Each Data write is:

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 4 | Absolute firmware offset, unsigned little-endian |
| 4 | remaining | Firmware bytes |

Offsets must be sequential and equal to the number of bytes already accepted. The
device rejects empty payloads, gaps, duplicates, overflow beyond the declared size,
and Data received outside the ready/receiving states.

With ATT MTU 256, the ATT write-value limit is 253 bytes. OTA v1 conservatively
caps the complete Data value at 249 bytes; after the four-byte offset, the default
firmware payload is therefore 245 bytes. The host helper uses a smaller value when
the active Bleak backend exposes a smaller negotiated limit.

### Status packet

Status is exactly 16 bytes:

| Offset | Size | Field | Encoding |
| ---: | ---: | --- | --- |
| 0 | 1 | protocol | `0x01` |
| 1 | 1 | state | State value below |
| 2 | 1 | last command | Last accepted/processed command byte |
| 3 | 1 | flags | Bit 0: Status subscribed; bit 1: NUS TX subscribed |
| 4 | 4 | written | Bytes committed/accepted, unsigned little-endian |
| 8 | 4 | total | Declared image size, unsigned little-endian |
| 12 | 4 | error | Stable signed little-endian OTA error code; zero on success |

| State | Value | Meaning |
| --- | ---: | --- |
| `IDLE` | 0 | No OTA session |
| `PREPARING` | 1 | Validating request/opening inactive partition |
| `READY` | 2 | Ready for offset zero |
| `RECEIVING` | 3 | Receiving/writing image bytes |
| `VERIFYING` | 4 | Final size, digest, and ESP image verification |
| `SUCCESS` | 5 | Image accepted and boot partition selected |
| `FAILED` | 6 | Session failed; `error` gives the reason |
| `ABORTED` | 7 | Session explicitly aborted or disconnected |

Clients must tolerate repeated status packets and unknown flag bits. A protocol
version mismatch is fatal.

| Error | Value | Meaning |
| --- | ---: | --- |
| `NONE` | 0 | No error |
| `NOT_AUTHORIZED` | 1 | This boot did not enter the maintenance window |
| `INVALID_STATE` | 2 | Operation is not valid in the current OTA state |
| `INVALID_ARGUMENT` | 3 | A command field or data offset is invalid |
| `INVALID_SIZE` | 4 | Image size, data length, or final byte count is invalid |
| `DIGEST_MISMATCH` | 5 | Received image does not match the declared SHA-256 |
| `NO_UPDATE_PARTITION` | 6 | No inactive OTA partition is available |
| `RESOURCE_EXHAUSTED` | 7 | A bounded queue or memory resource is unavailable |
| `TIMEOUT` | 8 | A bounded device-side operation timed out |
| `INTERNAL` | 127 | Unclassified flash, image-validation, or platform failure |

These values are part of the v1 wire contract. Raw ESP-IDF `esp_err_t` values are
logged on the device but are never exposed to clients.

## Transfer state machine

1. The client validates that the local file is non-empty, gets its size, and
   computes SHA-256 before connecting.
2. It connects and subscribes to Status.
3. It sends `BEGIN` and waits for `READY`. No Data is sent during `PREPARING`.
4. It sends sequential Data writes with response, starting at offset zero.
5. It sends `FINISH` only after all declared bytes were accepted.
6. The device verifies byte count, SHA-256, and ESP image metadata, then ends the
   OTA handle and selects the new boot partition.
7. The device reports `SUCCESS`, allows the result to reach the client, and reboots
   after a bounded delay.

Malformed input, a wrong offset, flash error, digest mismatch, invalid image,
explicit abort, or disconnect calls the OTA abort path and does not select the
target partition. Queue saturation rejects that GATT write without accepting its
offset, so a client may retry it. A timed-out client can send `QUERY` and read
Status.

## Host uploader

Install and run from the repository root:

```sh
python3 -m pip install -r scripts/ble-ota/requirements.txt
python3 scripts/ble-ota/ble-ota.py --scan
python3 scripts/ble-ota/ble-ota.py build/peak.bin
```

Use `--address` to bypass discovery or `--name PEAK-A1B2` to select a specific
display. The default `PEAK` name match is a prefix so uniquely suffixed devices are
discoverable. The helper reports the local digest, device state, transfer progress,
throughput, rejection errors, timeouts, and unexpected disconnects.

Protocol-only tests do not require Bleak:

```sh
python3 -m unittest discover -s scripts/ble-ota -p 'test_*.py'
```

## Security, authorization, and rollback plan

BLE link encryption and a SHA-256 sent by the uploader do not authenticate
firmware. The hash only detects transfer corruption when an attacker cannot replace
both the image and `BEGIN` packet. Before production release:

- Enable ESP-IDF signed-app verification for production OTA images and manage the
  signing key outside this repository.
- Enable rollback support and mark the new image valid only after bounded boot,
  application, BLE/hosted-controller, and essential hardware health checks pass.
- Preserve the explicit maintenance gate: accept `BEGIN` only in
  `BOOT_MODE_CONFIG`, entered by holding POWER+DOWN through the initial boot hold.
  BLE/NUS remains available in normal mode, while OTA `BEGIN` is rejected.
- Reject OTA while the vehicle is moving, input power is unsuitable, or another
  update/session owns the flash writer.
- The current release deliberately has no BLE pairing, bonding, encryption, or
  privacy address rotation: all GATT access is unauthenticated, and the OTA
  maintenance gate is not an authentication boundary. Do not enable individual
  SMP menuconfig options until a complete pairing, key-storage, and GATT
  authorization design is implemented and tested. Consider bonded/encrypted BLE
  as defense in depth, but do not use it as a substitute for signed firmware.
- Plan Secure Boot, flash encryption, and anti-rollback eFuse provisioning as a
  separate manufacturing/security rollout.

Signed-app verification, rollback configuration/health confirmation, Secure Boot,
flash encryption, and anti-rollback provisioning in this section are planned
hardening. They must not be assumed enabled unless the corresponding integration
and project configuration have been implemented and verified.

## Hardware test matrix

| Case | Expected result |
| --- | --- |
| VESC Tool discovery and sustained bridge traffic | NUS behavior remains compatible; no callback blocking or stream corruption |
| Valid image with Wi-Fi disabled | Full transfer, verification, slot switch, reboot, healthy boot |
| Corrupted image bytes | Digest/image verification failure; current slot remains bootable |
| Wrong declared digest | `FAILED`; current slot remains selected |
| Empty, truncated, or oversized input | Rejected without selecting the target slot |
| Duplicate, skipped, or out-of-order offset | Deterministic protocol error and aborted OTA handle |
| Disconnect during preparation/transfer/verification | No slot switch; reconnect starts at offset zero |
| Power removal at varied transfer offsets | Previously running firmware still boots |
| Power removal after slot switch | New image boots or bootloader rollback recovers |
| Deliberately failing first-boot health check | Rollback to previous valid slot |
| `BEGIN` in normal boot mode | Rejected with a stable authorization error; BLE/NUS remains usable |
| Hold POWER+DOWN through initial boot hold | Device enters `BOOT_MODE_CONFIG`; an otherwise valid `BEGIN` is accepted |
| Ten updates and repeated reconnect cycles | No stuck advertising, task leak, heap trend, or stale OTA ownership |
| Concurrent VESC activity and OTA attempt | Documented OTA policy enforced; local ESC safety behavior remains operational |
| Different phones/computers and MTUs | Chunk selection stays within negotiated limit and completes correctly |

Record end-to-end throughput, connection parameters, retransmissions, task stack
high-water marks, and heap before/after each stress run. A useful initial throughput
target is at least 15 KiB/s, subject to measurement on the P4/C6 transport.

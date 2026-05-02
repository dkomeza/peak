# VESC UDP Bridge Rewrite Design

## Goal

Rewrite the VESC CAN bridge and network transport from scratch for simpler
code and lower overhead. The new bridge uses UDP instead of TCP while keeping
VESC packet framing unchanged.

## Scope

In scope:

- Replace the current TCP bridge with a UDP endpoint on port `65102`.
- Rewrite the CAN wrapper around ESP-IDF TWAI.
- Rewrite the VESC bridge logic that translates between UDP-framed VESC packets
  and VESC CAN buffer commands.
- Keep one active UDP client. The newest sender becomes the active peer for
  responses.
- Preserve the current hardware assumptions: CAN RX GPIO `21`, CAN TX GPIO
  `22`, `500000` bit/s CAN bitrate, local bridge CAN id `254`, and target VESC
  CAN id `69`.

Out of scope:

- Updating `scripts/socket-test.py`.
- Supporting TCP compatibility.
- Supporting multiple UDP clients.
- Adding reliability, retries, sessions, acknowledgements, or ordering on top
  of UDP.
- Writing tests for this rewrite.

## Architecture

The rewrite uses three small modules with clear ownership:

- `main/wireless`: Wi-Fi station setup and UDP datagram IO.
- `main/can`: ESP-IDF TWAI setup and raw extended CAN frame IO.
- `main/vesc_bridge`: VESC packet parsing, CAN fragmentation, CAN reassembly,
  CRC validation, and response framing.

`main.c` should keep the same high-level startup flow: initialize NVS, inputs,
CAN, VESC bridge, Wi-Fi, then start the bridge. Function names can change where
they make boundaries clearer, but startup should stay direct and easy to read.

## Wireless Module

`main/wireless` exposes a UDP-oriented API:

```c
void wifi_init(void);
esp_err_t udp_bridge_start(udp_receive_callback_t callback, void *user_data);
esp_err_t udp_bridge_send(const uint8_t *data, size_t len);
```

The UDP receive task binds a `SOCK_DGRAM` socket to port `65102`. Each received
datagram updates the active peer address and passes the datagram bytes to the
registered callback. The callback receives raw bytes because VESC packet frames
can span datagrams or share a datagram.

`udp_bridge_send()` sends one UDP datagram to the active peer. If no client has
sent data yet, the function drops the packet without blocking. Repetitive drops
or socket errors should be logged with throttling so logs do not dominate the
runtime.

The module should avoid TCP-era complexity: no accept loop, no stream chunking,
no TCP send queue, and no partial-send loop. A mutex is only justified if the
socket and peer address are accessed concurrently in a way that ESP-IDF/lwIP
does not make safe.

## CAN Module

`main/can` exposes a small raw CAN API:

```c
esp_err_t can_bus_init(void);
esp_err_t can_bus_start(can_bus_receive_callback_t callback, void *user_data);
esp_err_t can_bus_send_ext(uint32_t id, const uint8_t *data, size_t len);
```

The module owns TWAI node creation, callback registration, RX queueing, and TX
submission. It should use fixed-size FreeRTOS queues and preallocated frame
storage when the TWAI API requires stable transmit memory. It should not allocate
memory after initialization.

Incoming frames are filtered to extended frames addressed to local bridge CAN id
`254`. Matching frames are dispatched through the receive callback. Outbound
frames are sent as extended frames and must reject payloads longer than eight
bytes.

When queues fill, the module should favor fresh data. Drop counters should be
logged with throttling.

## VESC Bridge Module

`main/vesc_bridge` exposes:

```c
esp_err_t vesc_bridge_init(void);
esp_err_t vesc_bridge_start(void);
```

Inbound UDP bytes are fed to the existing VESC packet parser
`packet_process_byte()`. Decoded VESC payloads are sent to target VESC CAN id
`69`.

Outbound payload-to-CAN fragmentation follows the VESC CAN command format:

- Payloads up to six bytes use `CAN_PACKET_PROCESS_SHORT_BUFFER`.
- Longer payloads are split into `CAN_PACKET_FILL_RX_BUFFER` frames for offsets
  through `255`, then `CAN_PACKET_FILL_RX_BUFFER_LONG` frames for larger offsets.
- The transfer is finalized with `CAN_PACKET_PROCESS_RX_BUFFER`, including
  sender id `254`, response mode `0`, payload length, and CRC.

Inbound CAN reassembly handles the same commands in reverse:

- `CAN_PACKET_FILL_RX_BUFFER` appends data at the expected one-byte offset.
- `CAN_PACKET_FILL_RX_BUFFER_LONG` appends data at the expected two-byte offset.
- `CAN_PACKET_PROCESS_RX_BUFFER` validates length and CRC, then forwards the
  reassembled payload.
- `CAN_PACKET_PROCESS_SHORT_BUFFER` forwards the inline payload directly.

Reassembled CAN payloads are wrapped with VESC packet framing via
`packet_send_packet()` and sent over UDP to the active peer.

Invalid lengths, unexpected offsets, oversize payloads, and CRC failures reset
the in-progress CAN response assembly. CRC failures and repetitive drops should
be logged with throttling.

## Performance Rules

- Keep FreeRTOS tasks focused and few: one UDP RX task, one CAN RX dispatch path,
  and only the TX tasking that TWAI needs.
- Use fixed-size buffers based on `PACKET_MAX_PL_LEN` and `CAN_MAX_DATA_LEN`.
- Avoid heap allocation after module initialization.
- Avoid copying when a callback can consume a stack or queue-owned frame
  directly.
- Prefer bounded nonblocking sends for freshness-sensitive bridge data.
- Keep logs useful but throttled.

## Verification

Per user instruction, this rewrite should not add or update tests.

Verification is limited to:

- Build the ESP-IDF application.
- Review compiler warnings and errors.
- Keep `scripts/socket-test.py` unchanged.

## Open Decisions

None. UDP port, framing, client model, and test scope are fixed by the design.

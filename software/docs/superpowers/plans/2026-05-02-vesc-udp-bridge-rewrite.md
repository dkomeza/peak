# VESC UDP Bridge Rewrite Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the existing VESC TCP/CAN bridge with a simpler UDP/CAN bridge on port `65102`.

**Architecture:** Rewrite the transport, CAN, and VESC bridge modules as small C APIs with fixed ownership. UDP owns datagrams and active peer tracking, CAN owns TWAI frame IO, and the VESC bridge owns packet framing plus CAN fragmentation/reassembly.

**Tech Stack:** ESP-IDF C, FreeRTOS tasks/queues, lwIP UDP sockets, ESP-IDF TWAI, existing VESC `packet` and `crc` helpers.

---

### Task 1: Rewrite Wireless UDP Transport

**Files:**
- Modify: `main/wireless/wifi.h`
- Modify: `main/wireless/wifi.c`

- [ ] Replace TCP API names and types with UDP bridge API.
- [ ] Keep Wi-Fi station initialization.
- [ ] Add a UDP RX task bound to port `65102`.
- [ ] Store the newest UDP sender as the active peer.
- [ ] Implement `udp_bridge_send()` with one `sendto()` per packet.
- [ ] Remove TCP accept loop, TCP TX queue, chunking, and `TCP_NODELAY`.

### Task 2: Rewrite CAN Bus Wrapper

**Files:**
- Modify: `main/can/can.h`
- Modify: `main/can/can.c`

- [ ] Replace public API with `can_bus_init()`, `can_bus_start()`, and `can_bus_send_ext()`.
- [ ] Keep CAN RX GPIO `21`, CAN TX GPIO `22`, and bitrate `500000`.
- [ ] Use ESP-IDF TWAI on-chip node setup.
- [ ] Filter received frames to extended frames addressed to local CAN id `254`.
- [ ] Dispatch matching received frames through the registered callback.
- [ ] Use fixed-size queues and preallocated TX slots.
- [ ] Favor fresh TX/RX data when queues fill.

### Task 3: Rewrite VESC Bridge

**Files:**
- Modify: `main/vesc_bridge/vesc_bridge.h`
- Modify: `main/vesc_bridge/vesc_bridge.c`

- [ ] Keep public API as `vesc_bridge_init()` and `vesc_bridge_start()`.
- [ ] Feed inbound UDP bytes into `packet_process_byte()`.
- [ ] Send decoded VESC payloads to target CAN id `69`.
- [ ] Fragment outbound payloads with VESC CAN buffer commands.
- [ ] Reassemble inbound CAN buffer commands addressed to local id `254`.
- [ ] Validate CRC before forwarding long CAN responses.
- [ ] Wrap CAN response payloads with `packet_send_packet()` and send over UDP.
- [ ] Throttle repetitive drop and CRC logs.

### Task 4: Update Integration

**Files:**
- Modify: `main/main.c`
- Modify: `main/CMakeLists.txt`

- [ ] Update includes and startup calls for renamed CAN/UDP APIs.
- [ ] Keep `scripts/socket-test.py` unchanged.
- [ ] Keep VESC `packet.c` and `crc.c` in the build.
- [ ] Remove obsolete source references only if files are renamed.

### Task 5: Build Verification

**Files:**
- No source edits expected.

- [ ] Run an ESP-IDF build command.
- [ ] Fix compile errors from the rewrite.
- [ ] Re-run the build command until it exits successfully or an external environment blocker is identified.

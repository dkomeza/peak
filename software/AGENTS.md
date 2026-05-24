# AGENTS.md

## Project Scope

This repository contains the embedded firmware for the Peak display/controller platform. It is an ESP-IDF C project using an ESP32-P4 as the main application/display chip, with an ESP32-C6 running ESP-Hosted for wireless work.

The firmware currently covers display rendering, wireless connectivity, OTA updates, IO sensors/buttons, CAN communication, ESC control, VESC bridging, and the `loom` graphics layer.

Treat this as embedded/realtime firmware, not a desktop application. Changes should preserve predictable memory use, clear task ownership, explicit error handling, and hardware-aware behavior.

## Hardware Architecture

- ESP32-P4 is the main chip and owns the primary application, display, IO, control, and orchestration work.
- ESP32-C6 is used for wireless via ESP-Hosted. Wi-Fi/BLE-related changes may involve host-side ESP-IDF code on the P4 and hosted-controller behavior on the C6.
- Be careful about transport boundaries between the P4 application and C6 wireless stack. Avoid assuming wireless operations are local, synchronous, or always available during boot.

## Architecture Guidance

The current architecture is allowed to evolve. Some sections may be early-stage, experimental, or not yet optimal. If a rewrite or meaningful refactor would make the firmware easier to maintain, easier to reason about, safer, or more testable, prefer proposing and doing that work instead of preserving weak structure.

When refactoring, keep the end state simple:
- Prefer small components with clear ownership.
- Keep hardware drivers, protocol code, UI/rendering, wireless hosting, and application orchestration separated.
- Avoid hiding timing, memory, or tasking behavior behind overly clever abstractions.
- Use explicit interfaces for interchangeable transports, controllers, renderers, and hardware backends.
- Keep public headers stable and minimal; put implementation details in component-private files.

## Embedded And Realtime Priorities

When making changes, consider:
- FreeRTOS task ownership, stack size, priority, and core affinity.
- Blocking calls inside callbacks, button handlers, BLE/Wi-Fi handlers, display paths, and protocol bridges.
- Heap allocation in long-running loops or timing-sensitive paths.
- DMA/internal memory requirements for display buffers and hardware peripherals.
- ESP-Hosted latency, initialization order, and disconnect/failure behavior.
- Clear `esp_err_t` propagation and useful `ESP_LOG*` messages.
- Failure behavior on hardware init, transport disconnects, OTA errors, and partial peripheral availability.

Do not silently ignore errors unless the failure is explicitly non-critical and logged or documented.

## Repository Layout

Important areas:
- `main/`: app entrypoint, boot/display/button orchestration, and top-level firmware behavior.
- `components/loom/`: immediate-mode graphics/rendering component for the display.
- `components/wireless/`: Wi-Fi, BLE, UDP bridge, BLE bridge, and BLE-triggered OTA.
- `components/vesc/`: VESC packet handling and bridge logic.
- `components/esc/`: ESC controller abstraction and implementations.
- `components/connection/`: CAN communication.
- `components/io/`: sensors, buttons, and battery monitoring.
- `components/peak_ota/`: OTA manager.
- `scripts/`: host-side helpers such as BLE OTA and font conversion.

## Build And Verification

Use ESP-IDF tooling from the repository root.

Common checks:
- `idf.py build`
- `idf.py flash monitor` when hardware verification is required and a board is connected.

For BLE OTA helper work:
- `python3 -m pip install -r scripts/ble-ota/requirements.txt`
- `python3 scripts/ble-ota/ble-ota.py --scan`
- `python3 scripts/ble-ota/ble-ota.py --serve build/peak.bin`

If ESP-IDF is not available in the shell, report that clearly and still perform static review where possible.

## Coding Standards

- Follow the existing C style: two-space indentation, `esp_err_t` returns for fallible operations, and ESP-IDF APIs/patterns.
- Prefer `ESP_ERROR_CHECK` only where crashing/restarting is acceptable during initialization or unrecoverable setup.
- For runtime paths, return errors or log warnings instead of aborting when recovery is possible.
- Keep callbacks short; push work into queues/tasks when it may block.
- Do not introduce dynamic allocation in hot paths without a clear reason.
- Avoid global mutable state unless it represents hardware/app singleton ownership and is simpler than passing context.
- Keep comments useful and sparse; explain non-obvious timing, memory, hardware, or protocol choices.

## Refactor Policy

Beneficial rewrites and refactors are in scope. This project values maintainability and clarity over preserving accidental structure.

Before large rewrites:
- Identify the concrete problem with the current design.
- Preserve behavior unless the behavior is intentionally being changed.
- Keep the rewrite bounded to one subsystem where possible.
- Build after each meaningful step.
- Prefer introducing a clean interface first, then migrating callers.

Do not perform unrelated cleanup while fixing a narrow bug unless the cleanup directly reduces risk for that bug.

## Agent Workflow

Before editing:
- Read the relevant component headers and CMake files.
- Check existing docs in `docs/superpowers/` when touching planned architecture such as `loom`.
- Inspect call sites before changing public APIs.

After editing:
- Run `idf.py build` when ESP-IDF is available.
- Mention any hardware behavior that still needs flashing or device testing.
- Leave unrelated user changes untouched.

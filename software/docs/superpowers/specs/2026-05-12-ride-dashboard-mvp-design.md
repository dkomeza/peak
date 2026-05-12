# Ride Dashboard MVP Design

Date: 2026-05-12

## Goal

Build the first production-shaped UI foundation for the Peak display firmware:
a ride dashboard MVP that uses mock data now, preserves the visual direction
from the reference, and leaves clean seams for KT and Peak/CycleIQ data sources
later.

The MVP should prioritize a glanceable riding experience on the 480x640
portrait display. It should not attempt to exactly reproduce every glow,
custom gauge, or font detail from the reference in the first pass.

## Non-Goals

- Wire live ESC, battery, sensor, or OTA data into the dashboard.
- Implement full settings, OTA, diagnostics, or menu screens.
- Build a custom font asset pipeline.
- Perfect the final gauge artwork or glow effects.

## Architecture

The display work will be split into three layers.

### Display Runtime

`main/display/display.c` remains responsible for hardware and LVGL runtime:

- ST7701/MIPI DSI panel bring-up.
- LVGL initialization.
- Tick source setup.
- Draw buffer allocation.
- Flush callback and panel transfer completion callback.
- LVGL task startup.

This layer should expose a small initialization API and should not own ride UI
objects or dashboard data.

### UI Layer

A new UI layer owns screens, styles, navigation, and UI update logic.

For the MVP, it will create a single dashboard screen and mock dashboard data.
It should be structured so additional screens can be added without returning
to a large monolithic `display.c`.

### Dashboard Data Model

The dashboard will consume a display-oriented data model, for example
`peak_dashboard_data_t`. This model is the future contract between real data
sources and the dashboard. It should contain fields such as:

- Speed and speed unit.
- Battery percent.
- Power in watts.
- Mode or support label.
- Connection state.
- Estimated range.
- Trip distance.
- Ride time.
- Average speed.
- Motor and controller temperatures.

For now, an LVGL timer can generate or cycle mock values. Later, KT and
Peak/CycleIQ adapters can both populate this model without making the
dashboard aware of ESC-specific details.

## Dashboard MVP

The first dashboard should preserve the reference hierarchy with simpler
embedded-friendly rendering.

### Top Status Row

The top-left status pill shows connection state, initially mock text such as
`ACTIVE`. The top-right pill shows battery percentage and a simple battery
indicator.

### Hero Area

The hero area contains:

- Mode/support label, such as `ECO - TORQUE`.
- Large speed value.
- Speed unit.
- Simplified circular arc or ring.

The first arc can use LVGL primitives. Custom glow, canvas rendering, or more
precise gauge artwork can be deferred.

### Power Pill

A centered pill below the speed shows current power in watts.

### Segment Row

A row of small horizontal segments sits under the hero. In the MVP this can be
decorative or reflect mock values. Later it can represent assist level, range,
or another ride metric.

### Info Cards

The lower section contains two medium cards:

- Estimated range.
- Thermal summary.

It also contains three compact cards:

- Trip distance.
- Ride time.
- Average speed.

## Visual System

The MVP should use a dark, high-contrast visual system inspired by the
reference:

- Dark background and panels.
- Bright green/cyan accent for active ride elements.
- White or light gray primary numeric text.
- Muted secondary labels.
- Orange or warm accent only where useful for thermal state.

Styles should be centralized through named colors, spacing values, font sizes,
and helper functions where practical. Avoid one-off inline style setup spread
through screen construction code.

Built-in LVGL fonts are acceptable for the MVP. Future work can add custom
numeric and label fonts to move closer to the reference.

## Behavior

`display_init()` should initialize the panel and LVGL runtime, then initialize
the UI.

All LVGL object creation and mutation should happen in LVGL context. The MVP
can keep mock data updates inside an LVGL timer to avoid cross-task UI writes.

Button handling can be introduced as stubs:

- Up/down may cycle mock values or future selectable regions.
- Power is reserved for future mode/menu behavior.

The first implementation should avoid designing the final navigation model
until more screens exist.

## Testing

Verification for the MVP:

- `idf.py build` passes.
- Dashboard code compiles without introducing live data dependencies.
- On hardware, the panel initializes and the dashboard renders without flush,
  allocation, or watchdog errors.

Future verification can add host-testable helpers for formatting dashboard
values once those helpers exist.

## Implementation Notes

Likely file structure:

- `main/display/display.h`
- `main/display/display.c`
- `main/ui/ui.h`
- `main/ui/ui.c`
- `main/ui/dashboard.h`
- `main/ui/dashboard.c`
- `main/ui/style.h`
- `main/ui/style.c`

`main/CMakeLists.txt` should include the new source files and LVGL/display
dependencies needed by the main component.

The implementation should first make the runtime reliable, then add the screen
structure and mock data. Visual refinements should stay within the MVP scope
unless required to prove the layout.

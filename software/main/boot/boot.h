#ifndef BOOT_H
#define BOOT_H

typedef enum BOOT_MODE {
  BOOT_MODE_UNDETERMINED = 0,
  BOOT_MODE_NORMAL,
  BOOT_MODE_MOUNTAIN,
  BOOT_MODE_CONFIG,
} boot_mode_t;

typedef void (*mountain_mode_callback_t)(void);

/**
 * This function requires POWER to remain pressed for one second. POWER + DOWN
 * then selects configuration mode.
 *
 * Holding POWER + UP at the end of that interval starts a non-blocking
 * three-second mountain-mode check. The callback is invoked only if both
 * buttons remain held for its full duration. The release of the initial POWER
 * hold is ignored by runtime button handlers.
 */
boot_mode_t boot(mountain_mode_callback_t callback);

#endif

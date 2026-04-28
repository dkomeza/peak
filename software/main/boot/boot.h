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
 * This function determines the boot mode of the device based on button presses
 * during boot. It will block until the mode was determined.
 *
 * If the user attempts to boot into mountain mode, it will handle it
 * asynchronously, and call the provided callback if mountain mode was selected.
 */
boot_mode_t boot(mountain_mode_callback_t callback);

#endif

#ifndef BOOT_H
#define BOOT_H

typedef enum BOOT_MODE {
  BOOT_MODE_UNDETERMINED = 0,
  BOOT_MODE_NORMAL,
  BOOT_MODE_MOUNTAIN,
  BOOT_MODE_CONFIG,
} boot_mode_t;

boot_mode_t boot(void);

#endif

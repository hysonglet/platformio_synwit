/*
 * SEGGER RTT configuration - belongs to the application, not to the library.
 *
 * SEGGER's repo keeps an (empty) copy in Config/, but PlatformIO only unpacks
 * the RTT/ subdirectory, so we ship our own. Empty means "use every default"
 * from RTT/SEGGER_RTT_ConfDefaults.h, which is what SEGGER ships too.
 *
 * The knobs you usually want to touch on a small part:
 *
 *   #define BUFFER_SIZE_UP                  (512)   // up-channel 0, default 1024
 *   #define BUFFER_SIZE_DOWN                (16)    // down-channel 0, default 16
 *   #define SEGGER_RTT_MAX_NUM_UP_BUFFERS   (1)     // default 3
 *   #define SEGGER_RTT_MAX_NUM_DOWN_BUFFERS (1)     // default 3
 *   #define SEGGER_RTT_PRINTF_BUFFER_SIZE   (64)    // default 64
 *   #define SEGGER_RTT_MODE_DEFAULT         SEGGER_RTT_MODE_BLOCK_IF_FIFO_FULL
 */
#ifndef SEGGER_RTT_CONF_H
#define SEGGER_RTT_CONF_H

#endif /* SEGGER_RTT_CONF_H */

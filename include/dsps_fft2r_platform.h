#pragma once
// PNGdec 1.1.6 ships optional ESP32-S3 assembly that includes this
// ESP-DSP private header. The ESP-DSP package is not installed here.
// Disable that optional routine; PNGdec uses its portable RGB565 path.
#ifndef dsps_fft2r_sc16_aes3_enabled
#define dsps_fft2r_sc16_aes3_enabled 0
#endif

#pragma once

/* WiFi SoftAP — join this network, then open http://192.168.4.1/ */
#define WIFI_AP_SSID       "CoilControl"
#define WIFI_AP_PASS       "coilcontrol"   /* ≥8 chars */
#define WIFI_AP_CHANNEL    6

/* Optional: also join a lab router (leave empty to SoftAP-only) */
#define WIFI_STA_SSID      ""
#define WIFI_STA_PASS      ""

#ifndef NUM_COILS
#define NUM_COILS          4
#endif
#if NUM_COILS != 4
#error "This firmware drives four coils (M1–M4)"
#endif

#ifndef PWM_TARGET_HZ
#define PWM_TARGET_HZ      1000U
#endif

#define PWM_ARR            255U
#define UART_BAUD          115200U
#define CLI_LINE_MAX       128
#define CURRENT_LOOP_HZ    200U
#define SINE_UPDATE_HZ     1000U
#define SINE_LUT_SIZE      360
#define SINE_LUT_90        90u
#define NUM_ROTATE_COILS   4
#define PCA_NCHAN          8

#ifndef PCA9685_I2C_ADDR
#define PCA9685_I2C_ADDR   0x40u
#endif
#define PCA9685_OSC_HZ     25000000u

#define ROTATE_FREQ_MIN_HZ 0.01f
#define ROTATE_FREQ_MAX_HZ 50.0f
#ifndef ROTATE_RAMP_STEP
#define ROTATE_RAMP_STEP   1u
#endif
#ifndef REVOLVE_H_PCT_DEFAULT
#define REVOLVE_H_PCT_DEFAULT 100u
#endif
#ifndef REVOLVE_V_PCT_DEFAULT
#define REVOLVE_V_PCT_DEFAULT 100u
#endif
#define REVOLVE_HV_PCT_MIN 1u
#define REVOLVE_HV_PCT_MAX 200u
#ifndef TILT_GAMMA_DEFAULT_DEG
#define TILT_GAMMA_DEFAULT_DEG 45.0f
#endif
#ifndef TELEPLOT_HZ
#define TELEPLOT_HZ        50u
#endif
#ifndef MLX_POLL_HZ
#define MLX_POLL_HZ        50u
#endif
#ifndef MLX_CAL_SETTLE_MS
#define MLX_CAL_SETTLE_MS  80u
#endif
#ifndef MLX_CAL_AVG
#define MLX_CAL_AVG        8u
#endif

/* ESP32-WROOM-32 DevKit pins. 3.3 V I2C. Avoid flash GPIO6–11 and UART0 1/3. */
#define PIN_PCA_SDA        21
#define PIN_PCA_SCL        22
#define PIN_MLX_SDA        18
#define PIN_MLX_SCL        19
#define PIN_EN_M1          25
#define PIN_EN_M2          26
#define PIN_EN_M3          27
#define PIN_EN_M4          14

#define I2C_BUS_PCA        0
#define I2C_BUS_MLX        1
#define I2C_HZ             400000

#pragma once

/* WiFi SoftAP — join this network, then open http://192.168.4.1/ */
#define WIFI_AP_SSID       "CoilControl"
#define WIFI_AP_PASS       "coilcontrol"   /* ≥8 chars */
#define WIFI_AP_CHANNEL    6

/* Optional: also join a lab router (leave empty to SoftAP-only) */
#define WIFI_STA_SSID      ""
#define WIFI_STA_PASS      ""

/* UART to STM32 USART3: ESP32 TX→PD9, ESP32 RX←PD8, GND common */
#define STM32_UART_BAUD    115200
#define PIN_STM32_TX       17   /* ESP32 GPIO17 → STM32 PD9 (RX) */
#define PIN_STM32_RX       18   /* ESP32 GPIO18 ← STM32 PD8 (TX) */

#define NUM_COILS          6
#define STATUS_POLL_MS     400
#define STM32_CMD_TIMEOUT_MS 1500

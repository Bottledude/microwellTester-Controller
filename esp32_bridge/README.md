# Coil controller + ESP32 WiFi bridge

## STM32H743 (`src/main.c`)
- USART1 PA9/PA10 — Serial Monitor CLI (echo on)
- USART3 PD8/PD9 — ESP32 bridge CLI (echo off) + `json` status line

## ESP32-S3 N16R8 (`esp32_bridge/`)
```bash
pio run -d esp32_bridge -t upload
```

### Wiring (3.3 V, common GND)
| ESP32-S3 | STM32 H743 |
|----------|------------|
| GPIO17 (TX) | PD9 (USART3_RX) |
| GPIO18 (RX) | PD8 (USART3_TX) |
| GND | GND |

### Web UI
1. Join SoftAP **CoilControl** / **coilcontrol**
2. Open **http://192.168.4.1/**
3. Control coils live; Serial Monitor on USART1 still works in parallel

Edit WiFi / pins in `esp32_bridge/include/config.h`.

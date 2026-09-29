# ESP32-S3 WiFi bridge

Nested PlatformIO project for the SoftAP + web UI. See the **[root README](../README.md)** for full system docs, wiring, and CLI.

```bash
# from repo root
pio run -d esp32_bridge -t upload
```

Config: [`include/config.h`](include/config.h) (SSID, password, UART pins).

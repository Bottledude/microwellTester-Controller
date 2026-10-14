# ESP32-S3 WiFi bridge

Nested PlatformIO project for the SoftAP + web UI. See the **[root README](../README.md)** for full system docs, wiring, and CLI.

The Field tab drives **rotate Y** and **revolve X/Z/tilt** (30° four-coil inverse, live γ). The MLX card shows reconstructed centre \(B\) from the four behind-coil chips. That path is display-only; coil PWM stays geometric. Live Teleplot still uses the STM32 **USART1** Serial Monitor, not this SoftAP UART.

```bash
# from repo root
pio run -d esp32_bridge -t upload
```

Config: [`include/config.h`](include/config.h) (SSID, password, UART pins).

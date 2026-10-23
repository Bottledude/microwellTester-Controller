#include "i2c_bus.h"
#include "config.h"

#include <Arduino.h>
#include <Wire.h>

extern "C" void i2c_bus_init(void)
{
  Wire.begin((int)PIN_PCA_SDA, (int)PIN_PCA_SCL, (uint32_t)I2C_HZ);
  Wire.setTimeOut(20);
  Wire1.begin((int)PIN_MLX_SDA, (int)PIN_MLX_SCL, (uint32_t)I2C_HZ);
  Wire1.setTimeOut(20);
}

static TwoWire &bus_of(uint8_t bus)
{
  return (bus == I2C_BUS_MLX) ? Wire1 : Wire;
}

extern "C" uint8_t i2c_tx(uint8_t bus, uint8_t addr7, const uint8_t *data, uint16_t n)
{
  TwoWire &w = bus_of(bus);
  w.beginTransmission(addr7);
  if (n && data) {
    if (w.write(data, n) != n) {
      (void)w.endTransmission();
      return 0;
    }
  }
  return (w.endTransmission() == 0) ? 1 : 0;
}

extern "C" uint8_t i2c_rx(uint8_t bus, uint8_t addr7, uint8_t *data, uint16_t n)
{
  TwoWire &w = bus_of(bus);
  uint8_t got = (uint8_t)w.requestFrom((int)addr7, (int)n);
  if (got != n) return 0;
  for (uint16_t i = 0; i < n; i++) data[i] = (uint8_t)w.read();
  return 1;
}

extern "C" uint8_t i2c_ready(uint8_t bus, uint8_t addr7)
{
  TwoWire &w = bus_of(bus);
  w.beginTransmission(addr7);
  return (w.endTransmission() == 0) ? 1 : 0;
}

extern "C" void i2c_delay_ms(uint32_t ms)
{
  delay(ms);
}

extern "C" uint32_t i2c_millis(void)
{
  return millis();
}

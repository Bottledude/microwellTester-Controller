#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void i2c_bus_init(void);

/* 7-bit address. Returns 1 on success. */
uint8_t i2c_tx(uint8_t bus, uint8_t addr7, const uint8_t *data, uint16_t n);
uint8_t i2c_rx(uint8_t bus, uint8_t addr7, uint8_t *data, uint16_t n);
uint8_t i2c_ready(uint8_t bus, uint8_t addr7);
void i2c_delay_ms(uint32_t ms);
uint32_t i2c_millis(void);

#ifdef __cplusplus
}
#endif

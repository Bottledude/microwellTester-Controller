#include "platform_lock.h"

#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"

static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

void field_irq_lock(void)
{
  portENTER_CRITICAL(&s_mux);
}

void field_irq_unlock(void)
{
  portEXIT_CRITICAL(&s_mux);
}

// Azoteq IQS5xx custom driver wrapper
// Calls upstream driver functions directly - no pointing_device.c patch needed

#include "quantum.h"
#include "pointing_device.h"
#include "drivers/sensors/azoteq_iqs5xx.h"
#include "svalboard.h"
#include "pointing_detect.h"

// Azoteq can only report at 100Hz. Faster polling causes issues.
#define AZO_MS 10

static uint16_t azo_timer = 0;
static uint8_t azo_held_buttons = 0;
static uint16_t azo_cached_cpi = 0;

// Public in the driver but missing from its header.
i2c_status_t azoteq_iqs5xx_end_session(void);

static uint16_t azo_product = AZOTEQ_IQS5XX_UNKNOWN;

bool pointing_device_driver_init(void) {
    azoteq_iqs5xx_init();
    // The product number, read again here because init's result doesn't say whether one answered.
    azo_product = azoteq_iqs5xx_get_product();
    if (azo_product != AZOTEQ_IQS5XX_UNKNOWN) azoteq_iqs5xx_end_session();
    return true;
}

void sval_pointer_identity(sval_pointer_identity_t *id) {
    id->built_for = SVAL_SENSOR_AZOTEQ;
    if (azo_product != AZOTEQ_IQS5XX_UNKNOWN) {
        id->detected           = SVAL_SENSOR_AZOTEQ;
        id->product_id         = azo_product >> 8;
        id->inverse_product_id = azo_product & 0xFF;
        id->flags |= SVAL_POINTER_INIT_OK;
    } else {
        id->detected = SVAL_SENSOR_NO_RESPONSE;
    }
}

uint8_t sval_pointer_own_bus(void) {
    return SVAL_BUS_I2C;
}

report_mouse_t pointing_device_driver_get_report(report_mouse_t mouse_report) {
    if (timer_elapsed(azo_timer) >= AZO_MS) {
        azo_timer = timer_read();
        mouse_report = azoteq_iqs5xx_get_report(mouse_report);
        azo_held_buttons = mouse_report.buttons;
    }

    // Invert V-axis
    mouse_report.v = -mouse_report.v;
    mouse_report.buttons = azo_held_buttons;

    return mouse_report;
}

uint16_t pointing_device_driver_get_cpi(void) {
    return azo_cached_cpi;
}

void pointing_device_driver_set_cpi(uint16_t cpi) {
    if (cpi != azo_cached_cpi) {
        azoteq_iqs5xx_set_cpi(cpi);
        azo_cached_cpi = cpi;
    }
}

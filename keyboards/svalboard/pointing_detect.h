// Find which pointing module is attached, whatever the firmware was built for.
//
// The Svalboard's pointing modules share one connector but each uses its own pins and
// protocol: trackball (PMW3360/PMW3389, SPI on GP24-27), Azoteq trackpad (IQS5xx, I2C on
// GP16/17) and TrackPoint (PS/2 on GP23/24). When a build's own module doesn't answer,
// these probes check the others, so a host can name the right .uf2 (Sval 0x2C).
//
// The probes toggle the pins directly instead of using the chip's SPI/I2C/PIO
// peripherals, so they work in any build. They take each pin from whatever peripheral
// had it, so only run them when the build's own module didn't answer. GP25 is the
// trackball's chip select and the TrackPoint's (undriven) reset line: it's only pulled
// up, never driven, until the last probe (SPI), which runs only when PS/2 and I2C found
// nothing.
#pragma once

#include <stdbool.h>
#include <stdint.h>

// Buses a build's own driver already checked; the probes skip them.
#define SVAL_BUS_PS2 0x01
#define SVAL_BUS_I2C 0x02
#define SVAL_BUS_SPI 0x04

typedef struct {
    uint8_t detected; // enum sval_pointer_sensor: a module, or SVAL_SENSOR_NO_RESPONSE
    uint8_t id0;      // PMW Product_ID, Azoteq product number high byte, or PS/2 device ID
    uint8_t id1;      // PMW Inverse_Product_ID, or Azoteq product number low byte
} sval_probe_result_t;

// Probes the buses not in skip_buses, PS/2 then I2C then SPI, stopping at the first module.
sval_probe_result_t sval_probe_pointers(uint8_t skip_buses);

// Which trackball sensor a PMW Product_ID / Inverse_Product_ID pair names.
uint8_t sval_pmw33xx_classify(uint8_t product_id, uint8_t inverse_product_id);

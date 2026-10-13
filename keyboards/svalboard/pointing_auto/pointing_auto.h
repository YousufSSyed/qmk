// One firmware for every pointing module (SVAL_POINTING_AUTO): each module's driver is
// built in under its own names, and the one for whatever answered the boot probe runs.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "report.h"
#include "svalboard.h"

typedef struct {
    uint8_t sensor; // enum sval_pointer_sensor
    bool (*init)(void);
    report_mouse_t (*get_report)(report_mouse_t mouse_report);
    uint16_t (*get_cpi)(void);
    void (*set_cpi)(uint16_t cpi);
    void (*identity)(sval_pointer_identity_t *id);
    void (*rest_apply)(void);                // optional
    void (*status)(sval_idle_status_t *st);  // optional
    void (*recalibrate)(void);               // optional
} sval_pointer_driver_t;

extern const sval_pointer_driver_t sval_driver_pmw3360;
extern const sval_pointer_driver_t sval_driver_pmw3389;
extern const sval_pointer_driver_t sval_driver_azoteq;
extern const sval_pointer_driver_t sval_driver_trackpoint;

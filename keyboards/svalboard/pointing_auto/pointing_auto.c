// One firmware for every pointing module. Nothing runs until svalboard.c's boot probe
// (pointing_detect.h, ~1.5 s after boot) says what's attached; then that module's
// driver starts and handles everything. Changing modules takes a reset.
#include "quantum.h"
#include "pointing_device.h"
#include "pointing_detect.h"
#include "pointing_auto.h"

static const sval_pointer_driver_t *driver      = NULL;
static uint16_t                     pending_cpi = 0;

void sval_pointer_select(const sval_probe_result_t *found) {
    static const sval_pointer_driver_t *const drivers[] = {&sval_driver_pmw3360, &sval_driver_pmw3389, &sval_driver_azoteq, &sval_driver_trackpoint};
    if (driver) return;
    for (uint8_t i = 0; i < ARRAY_SIZE(drivers); i++) {
        if (drivers[i]->sensor != found->detected) continue;
        drivers[i]->init();
        driver = drivers[i];
        if (pending_cpi) driver->set_cpi(pending_cpi);
        return;
    }
}

bool pointing_device_driver_init(void) {
    return true; // the module starts once the probe has found it
}

report_mouse_t pointing_device_driver_get_report(report_mouse_t mouse_report) {
    return driver ? driver->get_report(mouse_report) : mouse_report;
}

uint16_t pointing_device_driver_get_cpi(void) {
    return driver ? driver->get_cpi() : pending_cpi;
}

void pointing_device_driver_set_cpi(uint16_t cpi) {
    pending_cpi = cpi;
    if (driver) driver->set_cpi(cpi);
}

void sval_pointer_identity(sval_pointer_identity_t *id) {
    if (driver) driver->identity(id);
    id->flags |= SVAL_POINTER_AUTO;
}

void sval_pointer_rest_apply(void) {
    if (driver && driver->rest_apply) driver->rest_apply();
}

void sval_pointer_status(sval_idle_status_t *st) {
    if (driver && driver->status) driver->status(st);
    else st->sensor_present = 0;
}

void recalibrate_pointer(void) {
    if (driver && driver->recalibrate) driver->recalibrate();
}

// azoteq/azoteq.c under its own names (pointing_auto.h).
#define pointing_device_driver_init sval_azoteq_drv_init
#define pointing_device_driver_get_report sval_azoteq_drv_get_report
#define pointing_device_driver_get_cpi sval_azoteq_drv_get_cpi
#define pointing_device_driver_set_cpi sval_azoteq_drv_set_cpi
#define sval_pointer_identity sval_azoteq_identity
#define sval_pointer_own_bus sval_azoteq_own_bus

// After the renames: headers it pulls in declare these names too.
#include "pointing_auto.h"
#include "../azoteq/azoteq.c"

const sval_pointer_driver_t sval_driver_azoteq = {
    .sensor     = SVAL_SENSOR_AZOTEQ,
    .init       = pointing_device_driver_init,
    .get_report = pointing_device_driver_get_report,
    .get_cpi    = pointing_device_driver_get_cpi,
    .set_cpi    = pointing_device_driver_set_cpi,
    .identity   = sval_pointer_identity,
};

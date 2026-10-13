// The PS/2 pointing module's driver functions, renamed for trackpoint.c to call (pointing_auto.h).
#define pointing_device_driver_init sval_ps2_drv_init
#define pointing_device_driver_get_report sval_ps2_drv_get_report
#define pointing_device_driver_get_cpi sval_ps2_drv_get_cpi
#define pointing_device_driver_set_cpi sval_ps2_drv_set_cpi

#include "../../../modules/svalboard/pointing_device_ps2/pointing_device_ps2.c"

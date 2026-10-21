// One image for both halves (side.h). The split link pins are chosen at run
// time (side.c): these two are the left half's, the default.
#define SERIAL_USART_TX_PIN GP0
#define SERIAL_USART_RX_PIN GP1
#define SERIAL_USART_FULL_DUPLEX
#define SERIAL_PIO_USE_PIO0
#define SERIAL_USART_PINS_RUNTIME

// Each half enumerates as its per-hand build did (svalboard/left, svalboard/right);
// a half with no side uses this target's own PID and name.
#define USB_PRODUCT_ID_RUNTIME
#define SVAL_EITHER_PID_LEFT 0x4044
#define SVAL_EITHER_PID_RIGHT 0x4045
#define SVAL_EITHER_PRODUCT_LEFT "Svalboard Left"
#define SVAL_EITHER_PRODUCT_RIGHT "Svalboard Right"

#include "../pointing_auto/config.h"

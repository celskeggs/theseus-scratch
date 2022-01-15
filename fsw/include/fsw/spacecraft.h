#ifndef APP_SPACECRAFT_H
#define APP_SPACECRAFT_H

#include <fsw/fakewire/exchange.h>
#include <fsw/fakewire/rmap.h>
#include <fsw/comm.h>
#include <fsw/magnetometer.h>
#include <fsw/radio.h>

typedef struct {
    // devices
    radio_t        radio;

    // telecomm infrastructure
    comm_dec_t     comm_decoder;
    comm_enc_t     comm_encoder;
} spacecraft_t;

extern magnetometer_t sc_mag;

void spacecraft_init(void);

#endif /* APP_SPACECRAFT_H */

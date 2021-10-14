#include <assert.h>
#include <stdio.h>

#include <hal/platform.h>
#include <fsw/clock.h>
#include <fsw/cmd.h>
#include <fsw/debug.h>
#include <fsw/spacecraft.h>
#include <fsw/tlm.h>

static rmap_addr_t radio_routing = {
    .destination = {
        .path_bytes = NULL,
        .num_path_bytes = 0,
        .logical_address = 41,
    },
    .source = {
        .path_bytes = NULL,
        .num_path_bytes = 0,
        .logical_address = 40,
    },
    .dest_key = 101,
};

static rmap_addr_t magnetometer_routing = {
    .destination = {
        .path_bytes = NULL,
        .num_path_bytes = 0,
        .logical_address = 42,
    },
    .source = {
        .path_bytes = NULL,
        .num_path_bytes = 0,
        .logical_address = 40,
    },
    .dest_key = 102,
};

static rmap_addr_t clock_routing = {
    .destination = {
        .path_bytes = NULL,
        .num_path_bytes = 0,
        .logical_address = 43,
    },
    .source = {
        .path_bytes = NULL,
        .num_path_bytes = 0,
        .logical_address = 40,
    },
    .dest_key = 103,
};

static bool initialized = false;
static spacecraft_t sc;

static void spacecraft_init(void) {
    assert(!initialized);

    debugf("Initializing fakewire infrastructure...");
    fw_link_options_t options = {
        .label = "bus",
        .path  = "/dev/vport0p1",
        .flags = FW_FLAG_VIRTIO,
    };
    int err = rmap_init_monitor(&sc.monitor, options, 0x2000);
    assert(err == 0);

    debugf("Initializing telecomm infrastructure...");
    ringbuf_init(&sc.uplink_ring, 0x4000, 1);
    ringbuf_init(&sc.downlink_ring, 0x4000, 1);
    comm_dec_init(&sc.comm_decoder, &sc.uplink_ring);
    comm_enc_init(&sc.comm_encoder, &sc.downlink_ring);
    telemetry_init(&sc.comm_encoder);

    debugf("Initializing clock...");
    clock_init(&sc.monitor, &clock_routing);

    debugf("Initializing radio...");
    radio_init(&sc.radio, &sc.monitor, &radio_routing, &sc.uplink_ring, &sc.downlink_ring);

    debugf("Initializing magnetometer...");
    magnetometer_init(&sc.mag, &sc.monitor, &magnetometer_routing);

    debugf("Initializing heartbeats...");
    heartbeat_init(&sc.heart);

    initialized = true;
}

int main(int argc, char *argv[]) {
    (void) argc;
    (void) argv;

    platform_init();

    debugf("Initializing...");

    spacecraft_init();

    debugf("Entering command main loop");

    cmd_mainloop(&sc);

    return 0;
}

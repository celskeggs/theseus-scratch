#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include "fakewire.h"

#define PORT_IO "/dev/ttyAMA1"

void fakewire_attach(fw_port_t *fwp, const char *path, int flags) {
    memset(fwp, 0, sizeof(fw_port_t));

    bit_buf_init(&fwp->readahead, FW_READAHEAD_LEN);
    fwp->parity_ok = true;

    fwp->writeahead_bits = 0;
    fwp->writeahead = 0;
    fwp->last_remainder = 0; // (either initialization should be fine)

    if (flags != FW_FLAG_SERIAL) {
        assert(flags == FW_FLAG_FIFO_CONS || flags == FW_FLAG_FIFO_PROD);
        // alternate mode for host testing via pipe

        char path_buf[strlen(path) + 10];
        snprintf(path_buf, sizeof(path_buf), "%s-c2p.pipe", path);
        int fd_c2p = open(path_buf, (flags == FW_FLAG_FIFO_CONS) ? O_WRONLY : O_RDONLY);
        snprintf(path_buf, sizeof(path_buf), "%s-p2c.pipe", path);
        int fd_p2c = open(path_buf, (flags == FW_FLAG_FIFO_PROD) ? O_WRONLY : O_RDONLY);

        if (fd_c2p < 0 || fd_p2c < 0) {
            perror("open");
            exit(1);
        }
        fwp->fd_in = (flags == FW_FLAG_FIFO_CONS) ? fd_p2c : fd_c2p;
        fwp->fd_out = (flags == FW_FLAG_FIFO_CONS) ? fd_c2p : fd_p2c;
    } else {
        fwp->fd_out = fwp->fd_in = open(path, O_RDWR | O_NOCTTY | O_NDELAY);
        if (fwp->fd_in < 0) {
            perror("open");
            exit(1);
        }
        fcntl(fwp->fd_in, F_SETFL, 0);

        struct termios options;

        if (tcgetattr(fwp->fd_in, &options) < 0) {
            perror("tcgetattr");
            exit(1);
        }

        cfsetispeed(&options, B9600);
        cfsetospeed(&options, B9600);

        // don't attach
        options.c_cflag |= CLOCAL | CREAD;

        // 8-bit data
        options.c_cflag &= ~CSIZE;
        options.c_cflag |= CS8;

        // raw input
        options.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);

        // raw output
        options.c_oflag &= ~OPOST;

        if (tcsetattr(fwp->fd_in, TCSANOW, &options) < 0) {
            perror("tcsetattr");
            exit(1);
        }
    }
    assert(fwp->fd_in != 0 && fwp->fd_out != 0);
}

void fakewire_detach(fw_port_t *fwp) {
    assert(fwp->fd_in != 0 && fwp->fd_out != 0);
    if (fwp->fd_in >= 0 && fwp->fd_in != fwp->fd_out) {
        if (close(fwp->fd_in) < 0) {
            perror("close");
            exit(1);
        }
        fwp->fd_in = -1;
    }
    if (fwp->fd_out >= 0) {
        if (close(fwp->fd_out) < 0) {
            perror("close");
            exit(1);
        }
        fwp->fd_out = -1;
    }
    bit_buf_destroy(&fwp->readahead);
}

static int count_ones(uint32_t value) {
    int count = 0;
    while (value) {
        count += (value & 1);
        value >>= 1;
    }
    return count;
}

static fw_char_t fakewire_parse_readbuf(fw_port_t *fwp) {
    if (!fwp->parity_ok) {
        return FW_PARITYFAIL;
    }
    size_t avail_bits = bit_buf_extractable_bits(&fwp->readahead);
    if (avail_bits < 6) {
        return -1;
    }
    uint32_t head = bit_buf_peek_bits(&fwp->readahead, 2);
    // ignore this parity bit... it was previously processed
    if (!(head & 2)) {
        if (avail_bits < 12) {
            // need more bits before parity can be validated
            return -1;
        }
        // data character
        fw_char_t dc = (bit_buf_extract_bits(&fwp->readahead, 10) >> 2);
        assert(dc == FW_DATA(dc));
        head = bit_buf_peek_bits(&fwp->readahead, 2);
        if (((count_ones(dc) + count_ones(head)) & 1) != 1) {
            // parity fail!
            fwp->parity_ok = false;
            return FW_PARITYFAIL;
        }
        return dc;
    } else {
        // control character
        fw_char_t control = bit_buf_extract_bits(&fwp->readahead, 4) >> 2;
        assert(control >= 0 && control <= 3);
        head = bit_buf_peek_bits(&fwp->readahead, 2);
        if (((count_ones(control) + count_ones(head)) & 1) != 1) {
            // parity fail!
            fwp->parity_ok = false;
            return FW_PARITYFAIL;
        }
        return FW_CTRL_FCT | control;
    }
}

fw_char_t fakewire_read(fw_port_t *fwp) {
    uint8_t readbuf[FW_READAHEAD_LEN];
    fw_char_t ch;
    while ((ch = fakewire_parse_readbuf(fwp)) < 0) {
        size_t count = bit_buf_insertable_bytes(&fwp->readahead);
        // if we can't parse yet, must have space to add more data!
        assert(count >= 1 && count <= FW_READAHEAD_LEN);
        ssize_t actual = read(fwp->fd_in, readbuf, count);
        if (actual < 0) {
            perror("read");
            exit(1);
        } else if (actual == 0) {
            // EOF... end of connection!
            fwp->parity_ok = false;
            return FW_PARITYFAIL;
        }
        assert(actual >= 1 && actual <= count);
        bit_buf_insert_bytes(&fwp->readahead, readbuf, actual);
    }
    return ch;
}

static void fakewire_write_bits(fw_port_t *fwp, uint32_t data, int nbits) {
    assert(0 <= fwp->writeahead_bits && fwp->writeahead_bits < 8);
    assert(nbits >= 1 && nbits <= 32);
    assert(fwp->writeahead_bits + nbits <= 32);
    data &= (1 << nbits) - 1;
    fwp->writeahead |= (data << fwp->writeahead_bits);
    fwp->writeahead_bits += nbits;
    while (fwp->writeahead_bits >= 8) {
        uint8_t c = fwp->writeahead & 0xFF;
        if (write(fwp->fd_out, &c, 1) < 1) {
            perror("write");
            exit(1);
        }
        fwp->writeahead >>= 8;
        fwp->writeahead_bits -= 8;
    }
}

void fakewire_write(fw_port_t *fwp, fw_char_t c) {
    int ctrl_bit = FW_IS_CTRL(c) ? 1 : 0;

    // [last:odd] [P] [C=0] -> P must be 0 to be odd!
    // [last:odd] [P] [C=1] -> P must be 1 to be odd!
    // [last:even] [P] [C=0] -> P must be 1 to be odd!
    // [last:even] [P] [C=1] -> P must be 0 to be odd!
    int parity_bit = fwp->last_remainder ^ ctrl_bit ^ 1;
    assert((parity_bit >> 1) == 0);

    uint32_t data_bits;
    int nbits;
    if (FW_IS_CTRL(c)) {
        assert(c >= FW_CTRL_FCT && c <= FW_CTRL_ESC);
        data_bits = c & 3;
        nbits = 2;
    } else {
        data_bits = FW_DATA(c);
        assert(c == data_bits);
        nbits = 8;
    }
    fakewire_write_bits(fwp, (data_bits << 2) | (ctrl_bit << 1) | parity_bit, nbits + 2);

    fwp->last_remainder = count_ones(data_bits) & 1;
}

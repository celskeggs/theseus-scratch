#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "ringbuf.h"

void ringbuf_init(ringbuf_t *rb, size_t capacity, size_t elem_size) {
    mutex_init(&rb->mutex);
    cond_init(&rb->cond);
    // make sure this is a power of two
    assert((capacity & (capacity - 1)) == 0);
    // make sure at least one bit is free
    assert((capacity << 1) != 0);
    assert(elem_size >= 1);

    rb->memory = malloc(capacity * elem_size);
    assert(rb->memory != NULL);
    rb->capacity = capacity;
    rb->elem_size = elem_size;
    rb->read_idx = rb->write_idx = 0;
}

// masks an unwrapped index into a valid array offset
static inline size_t mask(ringbuf_t *rb, size_t index) {
    return index & (rb->capacity - 1);
}

// _locked means the function assumes the lock is held
static inline size_t ringbuf_size_locked(ringbuf_t *rb) {
    size_t size = rb->write_idx - rb->read_idx;
    assert(size <= rb->capacity);
    return size;
}

static inline size_t ringbuf_space_locked(ringbuf_t *rb) {
    return rb->capacity - ringbuf_size_locked(rb);
}

size_t ringbuf_write(ringbuf_t *rb, void *data_in, size_t elem_count, ringbuf_flags_t flags) {
    mutex_lock(&rb->mutex);
    // first, if we're being asked to write more data than we can, limit it.
    size_t space = ringbuf_space_locked(rb);
    if (flags & RB_BLOCKING) {
        while (space == 0) {
            cond_wait(&rb->cond, &rb->mutex);
            space = ringbuf_space_locked(rb);
        }
    }
    if (elem_count > space) {
        elem_count = space;
    }
    if (elem_count > 0) {
        // might need up to two writes: a tail write, and a head write.
        size_t tail_write_index = mask(rb, rb->write_idx);
        // first, the tail write
        size_t tail_write_count = elem_count;
        if (tail_write_index + tail_write_count > rb->capacity) {
            tail_write_count = rb->capacity - tail_write_index;
        }
        assert(tail_write_count <= elem_count);
        memcpy(&rb->memory[tail_write_index * rb->elem_size], data_in, tail_write_count * rb->elem_size);
        // then, if necessary, the head write
        size_t head_write_count = elem_count - tail_write_count;
        if (head_write_count > 0) {
            memcpy(&rb->memory[0], data_in + tail_write_count * rb->elem_size, head_write_count * rb->elem_size);
        }
        rb->write_idx += elem_count;
        cond_broadcast(&rb->cond);
    }
    assert(ringbuf_space_locked(rb) + elem_count == space);
    mutex_unlock(&rb->mutex);
    return elem_count;
}

size_t ringbuf_read(ringbuf_t *rb, void *data_out, size_t elem_count, ringbuf_flags_t flags) {
    mutex_lock(&rb->mutex);
    // first, if we're being asked to read more data than we have, limit it.
    size_t size = ringbuf_size_locked(rb);
    while (size == 0 && (flags & RB_BLOCKING)) {
        cond_wait(&rb->cond, &rb->mutex);
        size = ringbuf_size_locked(rb);
    }
    if (elem_count > size) {
        elem_count = size;
    }
    if (elem_count > 0) {
        // might need up to two reads: a tail read, and a head read.
        size_t tail_read_index = mask(rb, rb->read_idx);
        // first, the tail read
        size_t tail_read_count = elem_count;
        if (tail_read_index + tail_read_count > rb->capacity) {
            tail_read_count = rb->capacity - tail_read_index;
        }
        assert(tail_read_count <= elem_count);
        memcpy(data_out, &rb->memory[tail_read_index * rb->elem_size], tail_read_count * rb->elem_size);
        // then, if necessary, the head read
        size_t head_read_count = elem_count - tail_read_count;
        if (head_read_count > 0) {
            memcpy(data_out + tail_read_count * rb->elem_size, &rb->memory[0], head_read_count * rb->elem_size);
        }
        rb->read_idx += elem_count;
        cond_broadcast(&rb->cond);
    }
    assert(ringbuf_size_locked(rb) + elem_count == size);
    mutex_unlock(&rb->mutex);
    return elem_count;
}

size_t ringbuf_size(ringbuf_t *rb) {
    mutex_lock(&rb->mutex);
    size_t size = ringbuf_size_locked(rb);
    mutex_unlock(&rb->mutex);
    return size;
}

size_t ringbuf_space(ringbuf_t *rb) {
    mutex_lock(&rb->mutex);
    size_t space = ringbuf_space_locked(rb);
    mutex_unlock(&rb->mutex);
    return space;
}

void ringbuf_write_all(ringbuf_t *rb, void *data_in, size_t elem_count) {
    while (elem_count > 0) {
        size_t sent = ringbuf_write(rb, data_in, elem_count, RB_BLOCKING);
        assert(sent > 0 && sent <= elem_count);
        elem_count -= sent;
        data_in += sent * rb->elem_size;
    }
}

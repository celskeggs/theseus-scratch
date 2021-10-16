#ifndef FSW_FREERTOS_HAL_THREAD_H
#define FSW_FREERTOS_HAL_THREAD_H

#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include <FreeRTOS.h>
#include <semphr.h>

#include <rtos/timer.h>

typedef struct {
    TaskHandle_t      handle;
    SemaphoreHandle_t done;
    void           *(*start_routine)(void*);
    void             *arg;
} *thread_t;
typedef SemaphoreHandle_t mutex_t;
typedef SemaphoreHandle_t semaphore_t;
typedef TaskHandle_t wakeup_t;
typedef QueueHandle_t queue_t;

extern void thread_create(thread_t *out, const char *name, unsigned int priority, void *(*start_routine)(void*), void *arg);
extern void thread_join(thread_t thread);
extern void thread_cancel(thread_t thread);
extern void thread_time_now(struct timespec *tp);
extern bool thread_join_timed(thread_t thread, const struct timespec *abstime); // true on success, false on timeout
extern void thread_disable_cancellation(void);
extern void thread_enable_cancellation(void);
extern void thread_testcancel(void);

extern void mutex_init(mutex_t *mutex);
extern void mutex_destroy(mutex_t *mutex);

static inline void mutex_lock(mutex_t *mutex) {
    BaseType_t status;
    assert(mutex != NULL && *mutex != NULL);
    status = xSemaphoreTake(*mutex, portMAX_DELAY);
    assert(status == pdTRUE); // should always be obtained, because we have support for vTaskSuspend
}

// returns true if taken, false if not available
static inline bool mutex_lock_try(mutex_t *mutex) {
    assert(mutex != NULL && *mutex != NULL);
    return xSemaphoreTake(*mutex, 0) == pdTRUE;
}

static inline void mutex_unlock(mutex_t *mutex) {
    BaseType_t status;
    assert(mutex != NULL && *mutex != NULL);
    status = xSemaphoreGive(*mutex);
    assert(status == pdTRUE); // should always be released, because we should have acquired it earlier
}

// semaphores are created empty, such that an initial take will block
extern void semaphore_init(semaphore_t *sema);
extern void semaphore_destroy(semaphore_t *sema);

static inline void semaphore_take(semaphore_t *sema) {
    BaseType_t status;
    assert(sema != NULL && *sema != NULL);
    status = xSemaphoreTake(*sema, portMAX_DELAY);
    assert(status == pdTRUE);
}

// returns true if taken, false if not available
static inline bool semaphore_take_try(semaphore_t *sema) {
    assert(sema != NULL && *sema != NULL);
    return xSemaphoreTake(*sema, 0) == pdTRUE;
}

// returns true if taken, false if timed out
static inline bool semaphore_take_timed(semaphore_t *sema, uint64_t nanoseconds) {
    assert(sema != NULL && *sema != NULL);
    return xSemaphoreTake(*sema, timer_ns_to_ticks(nanoseconds)) == pdTRUE;
}

static inline bool semaphore_give(semaphore_t *sema) {
    assert(sema != NULL && *sema != NULL);
    return xSemaphoreGive(*sema) == pdTRUE;
}

static inline wakeup_t wakeup_open(void) {
    TaskHandle_t task = xTaskGetCurrentTaskHandle();
    assert(task != NULL);
    xTaskNotifyStateClear(task);
    return task;
}

static inline void wakeup_take(wakeup_t wakeup) {
    assert(wakeup != NULL && wakeup == xTaskGetCurrentTaskHandle());
    BaseType_t status = ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    assert(status == 1);
}

// returns true if taken, false if timed out
// NOTE: on a timeout, the caller MUST ensure that the wakeup is never given in the future!
// (It is OK for the wakeup to be given immediately after return, as long as the thread calling wakeup_take_timed does
//  not perform any operations that could potentially use the thread-specific notification pathway.)
static inline bool wakeup_take_timed(wakeup_t wakeup, uint64_t nanoseconds) {
    assert(wakeup != NULL && wakeup == xTaskGetCurrentTaskHandle());
    BaseType_t status = ulTaskNotifyTake(pdTRUE, timer_ns_to_ticks(nanoseconds));
    assert(status == 0 || status == 1);
    return status == 1;
}

static inline void wakeup_give(wakeup_t wakeup) {
    assert(wakeup != NULL);
    xTaskNotifyGive(wakeup);
}

static inline void queue_init(queue_t *queue, size_t entry_size, size_t num_entries) {
    assert(queue != NULL);
    assert(entry_size > 0);
    assert(num_entries > 0);
    *queue = xQueueCreate(num_entries, entry_size);
    assert(*queue != NULL);
}

static inline void queue_destroy(queue_t *queue) {
    assert(queue != NULL && *queue != NULL);
    vQueueDelete(*queue);
    *queue = NULL;
}

static inline void queue_send(queue_t *queue, const void *new_item) {
    assert(queue != NULL && *queue != NULL);
    BaseType_t status;
    status = xQueueSend(*queue, new_item, portMAX_DELAY);
    assert(status == pdTRUE);
}

// returns true if sent, false if not
static inline bool queue_send_try(queue_t *queue, void *new_item) {
    assert(queue != NULL && *queue != NULL);
    BaseType_t status;
    status = xQueueSend(*queue, new_item, 0);
    return status == pdTRUE;
}

static inline void queue_recv(queue_t *queue, void *new_item) {
    assert(queue != NULL && *queue != NULL);
    BaseType_t status;
    status = xQueueReceive(*queue, new_item, portMAX_DELAY);
    assert(status == pdTRUE);
}

// returns true if received, false if not
static inline bool queue_recv_try(queue_t *queue, void *new_item) {
    assert(queue != NULL && *queue != NULL);
    BaseType_t status;
    status = xQueueReceive(*queue, new_item, 0);
    return status == pdTRUE;
}

// returns true if received, false if timed out
static inline bool queue_recv_timed_abs(queue_t *queue, void *new_item, uint64_t deadline_ns) {
    assert(queue != NULL && *queue != NULL);
    BaseType_t status;
    status = xQueueReceive(*queue, new_item, timer_ticks_until_ns(deadline_ns));
    return status == pdTRUE;
}

#endif /* FSW_FREERTOS_HAL_THREAD_H */

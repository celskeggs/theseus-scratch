#include <FreeRTOS.h>
#include <task.h>

#include <rtos/crash.h>
#include <rtos/scrubber.h>
#include <hal/atomic.h>
#include <hal/thread.h>
#include <fsw/debug.h>

#if ( configOVERRIDE_IDLE_TASK == 1 )
static thread_t idle_task_thread = NULL;
#endif

thread_t iter_first_thread = NULL;

static void thread_entrypoint(void *opaque) {
    thread_t state = (thread_t) opaque;

    if (state->hit_restart) {
        debugf(CRITICAL, "Pending restart on next scrubber cycle.");
#if ( configOVERRIDE_IDLE_TASK == 1 )
        scrubber_cycle_wait(state == idle_task_thread);
#else
        scrubber_cycle_wait(false);
#endif
    }

    task_clear_crash();

    // discard return value
    (void) state->start_routine(state->arg);

    BaseType_t status;
    status = xSemaphoreGive(state->done);
    assert(status == pdTRUE);

    // suspend here so that the current task can be deleted
    while (1) {
        vTaskSuspend(NULL);
    }
}

static void thread_start_internal(thread_t state) {
    state->handle = xTaskCreateStatic(thread_entrypoint, state->name, STACK_SIZE, state, state->priority,
                                      state->preallocated_stack, &state->preallocated_task_memory);
    assert(state->handle != NULL);
    // just a check for implementation assumptions regarding task creation
    assert((void*) state->handle == (void*) &state->preallocated_task_memory);

    vTaskSetApplicationTaskTag(state->handle, (void *) state);
}

void thread_restart_other_task(thread_t state) {
    assert(state != NULL && state->handle != NULL);
    assert(state->restartable == RESTARTABLE);
    assert(state->handle != xTaskGetCurrentTaskHandle());

    debugf(CRITICAL, "Restarting task '%s'", state->name);

    // this needs to be in a critical section so that there is no period of time in which other tasks could run AND
    // the TaskHandle could refer to undefined memory.
    taskENTER_CRITICAL();
    vTaskDelete(state->handle);
    state->hit_restart = true;
    thread_start_internal(state);
    taskEXIT_CRITICAL();

    debugf(CRITICAL, "Completed restart for task '%s'", state->name);
}

#if ( configOVERRIDE_IDLE_TASK == 1 )
extern void prvIdleTask(void *pvParameters);

static void *idle_task_main(void *opaque) {
    (void) opaque;
    prvIdleTask(NULL);
    return NULL;
}

void thread_idle_init(void) {
    assert(idle_task_thread == NULL);

    thread_create(&idle_task_thread, "IDLE", PRIORITY_IDLE, idle_task_main, NULL, RESTARTABLE);

    assert(idle_task_thread != NULL);
}
#else
void vApplicationGetIdleTaskMemory(StaticTask_t **ppxIdleTaskTCBBuffer, StackType_t **ppxIdleTaskStackBuffer,
                                   uint32_t *pulIdleTaskStackSize) {

    *ppxIdleTaskTCBBuffer = malloc(sizeof(StaticTask_t));
    assert(*ppxIdleTaskTCBBuffer != NULL);
    *ppxIdleTaskStackBuffer = malloc(sizeof(StackType_t) * configMINIMAL_STACK_SIZE);
    assert(*ppxIdleTaskStackBuffer != NULL);
    *pulIdleTaskStackSize = configMINIMAL_STACK_SIZE;
}
#endif

void thread_create(thread_t *out, const char *name, unsigned int priority,
                   void *(*start_routine)(void*), void *arg, restartable_t restartable) {
    assert(out != NULL);

    assert(priority < configMAX_PRIORITIES);

    thread_t state = malloc(sizeof(*state));
    assert(state != NULL);

    state->name = name;
    state->priority = priority;
    state->start_routine = start_routine;
    state->arg = arg;
    state->restartable = restartable;
    state->needs_restart = false;
    state->hit_restart = false;
    state->done = xSemaphoreCreateBinary();
    assert(state->done != NULL);
    state->handle = NULL;
    state->iter_next_thread = iter_first_thread;
    atomic_store(iter_first_thread, state);

    if (name == NULL) {
        name = "anonymous_thread";
    }

    thread_start_internal(state);

    *out = state;
}

void thread_join(thread_t thread) {
    BaseType_t status;
    assert(thread != NULL && thread->done != NULL && thread->handle != NULL);

    status = xSemaphoreTake(thread->done, portMAX_DELAY);
    assert(status == pdTRUE);

    vSemaphoreDelete(thread->done);
    vTaskDelete(thread->handle);

    thread->done = NULL;
    thread->handle = NULL;
}

void semaphore_init(semaphore_t *sema) {
    assert(sema != NULL);
    *sema = xSemaphoreCreateBinary();
    assert(*sema != NULL);
}

void semaphore_destroy(semaphore_t *sema) {
    assert(sema != NULL && *sema != NULL);
    vSemaphoreDelete(*sema);
    *sema = NULL;
}

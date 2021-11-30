#include <inttypes.h>

#include <FreeRTOS.h>
#include <task.h>

#include <rtos/arm.h>
#include <rtos/crash.h>
#include <rtos/gic.h>
#include <hal/thread.h>
#include <fsw/debug.h>

void abort(void) {
    asm volatile("CPSID i");
    shutdown_gic();
    while (1) {
        asm volatile("WFI");
    }
}

static __attribute__((noreturn)) void suspend_current_task(void) {
    for (;;) {
        debugf(CRITICAL, "SUSPENDING TASK.");
        // this will indeed suspend us in the middle of this abort handler... but that's fine! We don't actually need
        // to return all the way back to the interrupted task.
        vTaskSuspend(NULL);
        debugf(CRITICAL, "Aborted task unexpectedly woke up!");
    }
}

static bool task_restart_queue_initialized = false;
static queue_t task_restart_queue;
static thread_t task_restart_task;

static void restart_other_task(TaskHandle_t task) {
    assert(task != NULL && task != xTaskGetCurrentTaskHandle() && task != xTaskGetIdleTaskHandle());
    task_restart_hook_t *hook = (task_restart_hook_t *) xTaskGetApplicationTaskTag(task);
    assert(hook != NULL && hook->hook_callback != NULL);
    debugf(CRITICAL, "Performing restart action for task '%s'", pcTaskGetName(task));
    hook->hook_callback(hook->hook_param, task);
    debugf(CRITICAL, "Finished performing restart action for task '%s'", pcTaskGetName(task));
}

static void *restart_task_mainloop(void *opaque) {
    (void) opaque;

    TaskHandle_t task = NULL;
    for (;;) {
        queue_recv(&task_restart_queue, &task);
        restart_other_task(task);
    }
    return NULL;
}

void task_set_restart_handler(TaskHandle_t task, task_restart_hook_t *handler) {
    assert(handler != NULL);
    vTaskSetApplicationTaskTag(task, (TaskHookFunction_t) handler);
}

static __attribute__((noreturn)) void restart_current_task(void) {
    TaskHandle_t cur = xTaskGetCurrentTaskHandle();
    if (xTaskGetApplicationTaskTag(cur) != NULL) {
        // we can't restart ourself, but we can ask the restart task to restart us
        assert(task_restart_queue_initialized == true);
        queue_send(&task_restart_queue, &cur);
    } else {
        debugf(CRITICAL, "Cannot restart this task (not marked as RESTARTABLE); suspending instead.");
    }
    // wait forever for the restart task to run
    suspend_current_task();
}

void task_restart_init(void) {
    queue_init(&task_restart_queue, sizeof(TaskHandle_t), 1);
    task_restart_queue_initialized = true;
    thread_create(&task_restart_task, "restart-task", PRIORITY_REPAIR, restart_task_mainloop, NULL, NOT_RESTARTABLE);
}

struct reg_state {
    uint32_t r0;
    uint32_t r1;
    uint32_t r2;
    uint32_t r3;
    uint32_t r4;
    uint32_t r5;
    uint32_t r6;
    uint32_t r7;
    uint32_t r8;
    uint32_t r9;
    uint32_t r10;
    uint32_t r11;
    uint32_t r12;
    uint32_t lr;
};
static_assert(sizeof(struct reg_state) == 14 * 4, "invalid sizeof(struct reg_state)");

static const char *trap_mode_names[3] = {
    "UNDEFINED INSTRUCTION",
    "PREFETCH ABORT",
    "DATA ABORT",
};

void exception_report(uint32_t spsr, struct reg_state *state, unsigned int trap_mode) {
    uint64_t now = timer_now_ns();

    const char *trap_name = trap_mode < 3 ? trap_mode_names[trap_mode] : "???????";
    debugf(CRITICAL, "%s", trap_name);
    TaskHandle_t failed_task = xTaskGetCurrentTaskHandle();
    const char *name = pcTaskGetName(failed_task);
    debugf(CRITICAL, "%s occurred in task '%s' at PC=0x%08x SPSR=0x%08x", trap_name, name, state->lr, spsr);
    debugf(CRITICAL, "Registers:  R0=0x%08x  R1=0x%08x  R2=0x%08x  R3=0x%08x", state->r0, state->r1, state->r2, state->r3);
    debugf(CRITICAL, "Registers:  R4=0x%08x  R5=0x%08x  R6=0x%08x  R7=0x%08x", state->r4, state->r5, state->r6, state->r7);
    debugf(CRITICAL, "Registers:  R8=0x%08x  R9=0x%08x R10=0x%08x R11=0x%08x", state->r8, state->r9, state->r10, state->r11);
    debugf(CRITICAL, "Registers: R12=0x%08x", state->r12);
    debugf(CRITICAL, "HALTING RTOS IN REACTION TO %s AT TIME=%" PRIu64, trap_name, now);
    // returns to an abort() call
}

// defined in entrypoint.s
extern volatile uint32_t trap_recursive_flag;

static volatile TaskHandle_t last_failed_task = NULL;

void task_abort_handler(unsigned int trap_mode) {
    const char *trap_name = trap_mode < 3 ? trap_mode_names[trap_mode] : "???????";
    debugf(CRITICAL, "TASK %s", trap_name);
    TaskHandle_t failed_task = xTaskGetCurrentTaskHandle();
    assert(failed_task != NULL);
    const char *name = pcTaskGetName(failed_task);
    debugf(CRITICAL, "%s occurred in task '%s'", trap_name, name);

    if (failed_task == xTaskGetIdleTaskHandle()) {
        // cannot suspend the IDLE task safely, because FreeRTOS requires that there always be an IDLE task
        abortf("EXCEPTION OCCURRED IN IDLE TASK; HALTING RTOS.");
    }

    if (last_failed_task == failed_task) {
        // should be different, because we shouldn't hit any aborts past this point
        abortf("RECURSIVE ABORT; HALTING RTOS.");
    }

    last_failed_task = failed_task;

    portMEMORY_BARRIER(); // so that we commit our changes to last_failed_task before updating the recursive flag

    assert(trap_recursive_flag == 1);
    trap_recursive_flag = 0;

    // this will indeed suspend us in the middle of this abort handler... but that's fine! We don't actually need
    // to return all the way back to the interrupted task.
    restart_current_task();
}

void vApplicationStackOverflowHook(TaskHandle_t task, char *pcTaskName) {
    (void) task;

    uint64_t now = timer_now_ns();

    debugf(CRITICAL, "STACK OVERFLOW occurred in task '%s'", pcTaskName);
    abortf("HALTING IN REACTION TO STACK OVERFLOW AT TIME=%" PRIu64, now);
}

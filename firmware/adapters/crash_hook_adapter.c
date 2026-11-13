/*
 * Crash note — spec/30-processes/health-monitoring.md § Crash note.
 * The SDK exception handler calls our init callback first; we write the
 * running task's name into the retained log ring, which survives the reset
 * that follows (watchdog or reboot), and shows as the previous-boot log.
 */
#include "FreeRTOS.h"
#include "task.h"

#include "app_log.h"
#include "crash_hook_adapter.h"
#include "exception_handler.h"

static void crash_hook_note(void)
{
    const char *name = NULL;

    if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED) {
        name = pcTaskGetTaskName(NULL);
    }
    app_log_crash_note(name != NULL ? name : "(no task)");
}

void crash_hook_adapter_init(void)
{
    static exception_config_type cb = { crash_hook_note, NULL };

    (void)exception_register_callbacks(&cb);
}

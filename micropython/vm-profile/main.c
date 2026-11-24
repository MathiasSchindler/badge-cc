#include "py/cstack.h"
#include "py/gc.h"
#include "py/runtime.h"
#include "shared/runtime/pyexec.h"
#include "shared/runtime/semihosting_arm.h"
#include "workload.h"

static uint32_t heap[MICROPY_HEAP_SIZE / sizeof(uint32_t)];

void __wrap_uart_init(void) {}
void __wrap_ticks_init(void) {}

void __wrap_uart_tx_strn(const char *text, size_t length) {
    mp_semihosting_tx_strn(text, length);
}

int __wrap_main(void) {
    mp_cstack_init_with_sp_here(MICROPY_STACK_SIZE);
    gc_init(heap, heap + MP_ARRAY_SIZE(heap));
    mp_init();
    vstr_t input;
    vstr_init_fixed_buf(&input, sizeof(workload), workload);
    input.len = sizeof(workload) - 1;
    if (pyexec_vstr(&input, false) != PYEXEC_NORMAL_EXIT) {
        mp_semihosting_exit(1);
        return 1;
    }
    static const char success[] = "VM_REPLAY_OK\n";
    mp_semihosting_tx_strn(success, sizeof(success) - 1);
    return 0;
}

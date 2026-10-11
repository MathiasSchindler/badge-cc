#ifndef UNIVERSE26_COMPILER_APPLICATION_H
#define UNIVERSE26_COMPILER_APPLICATION_H
#include "suite.h"

void compiler_application_prepare(uint32_t module, uint32_t seed);
void compiler_application_run(uint32_t module);
int compiler_application_result(uint32_t module, compiler_result_t *out);
int compiler_application_measure(uint32_t module, uint32_t seed,
    compiler_result_t *out, uint32_t (*clock)(void *),
    void (*service)(void *), void *context, uint32_t samples[7]);
#endif

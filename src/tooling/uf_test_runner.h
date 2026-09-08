#ifndef UF_TEST_RUNNER_H
#define UF_TEST_RUNNER_H

#include "../common/uf_common.h"

typedef struct {
    bool use_vm;
    bool use_regvm;
    bool strict;
    bool verbose;
    const char* filter;
} UfTestOptions;

/* Run test runner on file or directory path. Returns 0 on all tests passing, 1 if any failed. */
int uf_test_run(const char* target_path, const UfTestOptions* options);

/* Resolve the path to the unfish binary */
const char* uf_find_self_binary(void);

#endif /* UF_TEST_RUNNER_H */

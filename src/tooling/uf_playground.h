#ifndef UF_PLAYGROUND_H
#define UF_PLAYGROUND_H

#include "../common/uf_common.h"

typedef struct {
    int port;
    const char* web_root;
    bool open_browser;
} UfPlaygroundOptions;

/* Start the local Web Playground HTTP server on the given port.
   Returns 0 on clean exit, non-zero on failure. */
int uf_playground_start(const UfPlaygroundOptions* options);

#endif /* UF_PLAYGROUND_H */

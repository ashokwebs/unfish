#ifndef UF_COMMON_H
#define UF_COMMON_H

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#define UF_UNUSED(x) ((void)(x))

#define UF_VERSION_STRING "0.8.0-alpha"

#endif /* UF_COMMON_H */

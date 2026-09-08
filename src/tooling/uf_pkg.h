#ifndef UF_PKG_H
#define UF_PKG_H

#include "../common/uf_common.h"

int uf_pkg_init(const char* name);
int uf_pkg_check(void);
int uf_pkg_run(int argc, char** argv);
int uf_pkg_test(void);
int uf_pkg_build(const char* out_bin);

#endif /* UF_PKG_H */

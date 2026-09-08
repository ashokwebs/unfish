#ifndef UF_LEARN_H
#define UF_LEARN_H

#include "../common/uf_common.h"

/* Launch interactive Unfish CLI tutorial */
int uf_learn_start(int starting_lesson);

/* List available tutorial lessons */
void uf_learn_list(void);

#endif /* UF_LEARN_H */

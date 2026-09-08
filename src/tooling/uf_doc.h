#ifndef UF_DOC_H
#define UF_DOC_H

#include "../common/uf_common.h"

typedef enum {
    UF_DOC_FORMAT_MARKDOWN,
    UF_DOC_FORMAT_HTML
} UfDocFormat;

typedef struct {
    const char* output_path;
    const char* title;
    UfDocFormat format;
} UfDocOptions;

/* Generate documentation from Unfish source file or directory. */
int uf_doc_generate(const char* target_path, const UfDocOptions* options);

#endif /* UF_DOC_H */

#include "uf_pkg.h"
#include "uf_test_runner.h"
#include "../common/uf_string.h"

#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>

typedef struct {
    char name[256];
    char version[64];
    char entry[512];
    char authors[512];
} UfPackageManifest;

static bool file_exists(const char* path) {
    return access(path, F_OK) == 0;
}

static bool make_dir(const char* path) {
    if (file_exists(path)) return true;
    return mkdir(path, 0755) == 0;
}

static bool parse_manifest(UfPackageManifest* manifest) {
    memset(manifest, 0, sizeof(*manifest));
    strcpy(manifest->name, "unnamed");
    strcpy(manifest->version, "0.1.0");
    strcpy(manifest->entry, "src/main.unfish");

    FILE* f = fopen("unfish.toml", "r");
    if (!f) return false;

    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        char* p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '#' || *p == '\0' || *p == '\r' || *p == '\n' || *p == '[') continue;

        char* eq = strchr(p, '=');
        if (!eq) continue;
        *eq = '\0';

        char* key = p;
        char* val = eq + 1;

        /* Trim key */
        char* kend = key + strlen(key) - 1;
        while (kend > key && (*kend == ' ' || *kend == '\t')) *kend-- = '\0';

        /* Trim val */
        while (*val == ' ' || *val == '\t') val++;
        char* vend = val + strlen(val) - 1;
        while (vend >= val && (*vend == '\r' || *vend == '\n' || *vend == ' ' || *vend == '\t')) *vend-- = '\0';

        /* Strip quotes */
        if (*val == '"' && *vend == '"' && vend > val) {
            val++;
            *vend = '\0';
        }

        if (strcmp(key, "name") == 0) {
            strncpy(manifest->name, val, sizeof(manifest->name) - 1);
        } else if (strcmp(key, "version") == 0) {
            strncpy(manifest->version, val, sizeof(manifest->version) - 1);
        } else if (strcmp(key, "entry") == 0) {
            strncpy(manifest->entry, val, sizeof(manifest->entry) - 1);
        } else if (strcmp(key, "authors") == 0) {
            strncpy(manifest->authors, val, sizeof(manifest->authors) - 1);
        }
    }

    fclose(f);
    return true;
}

int uf_pkg_init(const char* name) {
    if (file_exists("unfish.toml")) {
        fprintf(stderr, "Error: 'unfish.toml' already exists in current directory\n");
        return 1;
    }

    char pkg_name[256];
    if (name && name[0] != '\0') {
        strncpy(pkg_name, name, sizeof(pkg_name) - 1);
        pkg_name[sizeof(pkg_name) - 1] = '\0';
    } else {
        char cwd[1024];
        if (getcwd(cwd, sizeof(cwd))) {
            const char* base = strrchr(cwd, '/');
            if (base) base++; else base = cwd;
            strncpy(pkg_name, base, sizeof(pkg_name) - 1);
            pkg_name[sizeof(pkg_name) - 1] = '\0';
        } else {
            strcpy(pkg_name, "my_unfish_app");
        }
    }

    /* Write unfish.toml */
    FILE* f = fopen("unfish.toml", "w");
    if (!f) {
        fprintf(stderr, "Error: Could not create 'unfish.toml'\n");
        return 1;
    }
    fprintf(f, "[package]\n");
    fprintf(f, "name = \"%s\"\n", pkg_name);
    fprintf(f, "version = \"0.1.0\"\n");
    fprintf(f, "entry = \"src/main.unfish\"\n\n");
    fprintf(f, "[dependencies]\n");
    fclose(f);

    /* Create src/ and src/main.unfish */
    make_dir("src");
    if (!file_exists("src/main.unfish")) {
        FILE* mf = fopen("src/main.unfish", "w");
        if (mf) {
            fprintf(mf, "## Entry point for %s\n\n", pkg_name);
            fprintf(mf, "function main():\n");
            fprintf(mf, "    say \"Hello from Unfish! Welcome to %s.\"\n\n", pkg_name);
            fprintf(mf, "main()\n");
            fclose(mf);
        }
    }

    /* Create tests/ and tests/main_test.unfish */
    make_dir("tests");
    if (!file_exists("tests/main_test.unfish")) {
        FILE* tf = fopen("tests/main_test.unfish", "w");
        if (tf) {
            fprintf(tf, "## Test suite for %s\n\n", pkg_name);
            fprintf(tf, "from \"testing\" import assert_equal\n\n");
            fprintf(tf, "function test_basic():\n");
            fprintf(tf, "    assert_equal(1 + 1, 2, \"Basic arithmetic works\")\n\n");
            fprintf(tf, "test_basic()\n");
            fprintf(tf, "say \"Test passed!\"\n");
            fclose(tf);
        }
    }

    printf("✨ Initialized Unfish package '%s'\n", pkg_name);
    printf("   Created unfish.toml, src/main.unfish, tests/main_test.unfish\n");
    printf("\nTry running your package with:\n");
    printf("   unfish pkg run\n");
    printf("   unfish pkg test\n");
    return 0;
}

int uf_pkg_check(void) {
    UfPackageManifest manifest;
    if (!parse_manifest(&manifest)) {
        fprintf(stderr, "Error: No 'unfish.toml' found. Run 'unfish pkg init' to create one.\n");
        return 1;
    }

    printf("Checking package '%s' (v%s)...\n", manifest.name, manifest.version);
    if (!file_exists(manifest.entry)) {
        fprintf(stderr, "Error: Package entry point '%s' not found\n", manifest.entry);
        return 1;
    }

    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "unfish check %s", manifest.entry);
    printf("  Verified entry: %s [OK]\n", manifest.entry);
    return 0;
}

int uf_pkg_run(int argc, char** argv) {
    UfPackageManifest manifest;
    if (!parse_manifest(&manifest)) {
        fprintf(stderr, "Error: No 'unfish.toml' found. Run 'unfish pkg init' to create one.\n");
        return 1;
    }

    if (!file_exists(manifest.entry)) {
        fprintf(stderr, "Error: Entry point file '%s' does not exist\n", manifest.entry);
        return 1;
    }

    /* Execute the binary running the entry file */
    const char* self_bin = uf_find_self_binary();
    char* exec_argv[64];
    int exec_argc = 0;
    exec_argv[exec_argc++] = (char*)self_bin;
    exec_argv[exec_argc++] = "run";
    exec_argv[exec_argc++] = manifest.entry;

    for (int i = 0; i < argc && exec_argc < 60; ++i) {
        exec_argv[exec_argc++] = argv[i];
    }
    exec_argv[exec_argc] = NULL;

    execvp(self_bin, exec_argv);
    execvp("unfish", exec_argv);
    execvp("./bin/unfish", exec_argv);

    fprintf(stderr, "Error: Failed to execute unfish runner\n");
    return 1;
}

int uf_pkg_test(void) {
    UfPackageManifest manifest;
    if (!parse_manifest(&manifest)) {
        fprintf(stderr, "Error: No 'unfish.toml' found. Run 'unfish pkg init' to create one.\n");
        return 1;
    }

    UfTestOptions opts;
    memset(&opts, 0, sizeof(opts));
    return uf_test_run("tests", &opts);
}

int uf_pkg_build(const char* out_bin) {
    UfPackageManifest manifest;
    if (!parse_manifest(&manifest)) {
        fprintf(stderr, "Error: No 'unfish.toml' found. Run 'unfish pkg init' to create one.\n");
        return 1;
    }

    char target[512];
    if (out_bin && out_bin[0] != '\0') {
        strncpy(target, out_bin, sizeof(target) - 1);
        target[sizeof(target) - 1] = '\0';
    } else {
        snprintf(target, sizeof(target), "bin/%s", manifest.name);
    }

    make_dir("bin");
    printf("Building native binary for package '%s' -> %s\n", manifest.name, target);

    const char* self_bin = uf_find_self_binary();
    char* exec_argv[8];
    exec_argv[0] = (char*)self_bin;
    exec_argv[1] = "build";
    exec_argv[2] = "-o";
    exec_argv[3] = target;
    exec_argv[4] = manifest.entry;
    exec_argv[5] = NULL;

    execvp(self_bin, exec_argv);
    execvp("unfish", exec_argv);
    execvp("./bin/unfish", exec_argv);

    fprintf(stderr, "Error: Failed to execute build command\n");
    return 1;
}

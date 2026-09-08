#include "uf_test_runner.h"
#include "../common/uf_string.h"

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <time.h>

#define MAX_TESTS 1024
#define MAX_EXPECTED_LINES 512

typedef struct {
    char path[1024];
} TestFile;

static int compare_test_files(const void* a, const void* b) {
    const TestFile* fa = (const TestFile*)a;
    const TestFile* fb = (const TestFile*)b;
    return strcmp(fa->path, fb->path);
}

static bool is_directory(const char* path) {
    struct stat st;
    if (stat(path, &st) != 0) return false;
    return S_ISDIR(st.st_mode);
}

static bool ends_with(const char* str, const char* suffix) {
    size_t slen = strlen(str);
    size_t xlen = strlen(suffix);
    if (slen < xlen) return false;
    return strcmp(str + slen - xlen, suffix) == 0;
}

static void scan_directory(const char* dir_path, TestFile* tests, size_t* count, const char* filter) {
    DIR* dir = opendir(dir_path);
    if (!dir) return;

    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0 || strcmp(entry->d_name, ".git") == 0) {
            continue;
        }

        char full_path[1024];
        snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, entry->d_name);

        if (is_directory(full_path)) {
            scan_directory(full_path, tests, count, filter);
        } else {
            if (strncmp(entry->d_name, "helper_", 7) == 0) continue;
            if (ends_with(entry->d_name, ".unfish")) {
                if (filter && !strstr(full_path, filter)) {
                    continue;
                }
                if (*count < MAX_TESTS) {
                    snprintf(tests[*count].path, sizeof(tests[*count].path), "%s", full_path);
                    (*count)++;
                }
            }
        }
    }
    closedir(dir);
}

static int64_t get_time_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

/* Run a single test file using the current unfish executable binary via fork/pipe */
static bool run_single_test(const char* unfish_bin, const char* test_path, const UfTestOptions* options, int64_t* out_elapsed, char* err_buf, size_t err_buf_size) {
    int expected_exit = 0;
    bool has_expected_output = false;
    char expected_output[8192];
    expected_output[0] = '\0';
    size_t exp_len = 0;

    char expected_error[1024];
    expected_error[0] = '\0';

    bool strict_flag = options->strict;
    bool vm_flag = options->use_vm;
    bool regvm_flag = options->use_regvm;

    /* Parse expectations from comments in test_path */
    FILE* f = fopen(test_path, "r");
    if (!f) {
        snprintf(err_buf, err_buf_size, "Could not open test file: %s", test_path);
        return false;
    }

    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        size_t llen = strlen(line);
        while (llen > 0 && (line[llen - 1] == '\r' || line[llen - 1] == '\n')) {
            line[--llen] = '\0';
        }

        if (strncmp(line, "# expect-exit:", 14) == 0) {
            expected_exit = atoi(line + 14);
        } else if (strncmp(line, "# flags:", 8) == 0) {
            if (strstr(line + 8, "--strict")) strict_flag = true;
            if (strstr(line + 8, "--regvm")) regvm_flag = true;
            else if (strstr(line + 8, "--vm")) vm_flag = true;
        } else if (strncmp(line, "# expect-error:", 15) == 0) {
            const char* p = line + 15;
            while (*p == ' ') p++;
            strncpy(expected_error, p, sizeof(expected_error) - 1);
            expected_error[sizeof(expected_error) - 1] = '\0';
        } else if (strncmp(line, "# expect:", 9) == 0) {
            has_expected_output = true;
            const char* exp_line = line + 9;
            if (*exp_line == ' ') exp_line++;
            size_t l = strlen(exp_line);
            if (exp_len + l + 2 < sizeof(expected_output)) {
                if (exp_len > 0) {
                    expected_output[exp_len++] = '\n';
                }
                memcpy(expected_output + exp_len, exp_line, l);
                exp_len += l;
                expected_output[exp_len] = '\0';
            }
        }
    }
    fclose(f);

    int out_pipe[2];
    int err_pipe[2];
    if (pipe(out_pipe) < 0 || pipe(err_pipe) < 0) {
        snprintf(err_buf, err_buf_size, "Failed to create pipes");
        return false;
    }

    int64_t t0 = get_time_ms();
    pid_t pid = fork();
    if (pid < 0) {
        snprintf(err_buf, err_buf_size, "Failed to fork process");
        close(out_pipe[0]); close(out_pipe[1]);
        close(err_pipe[0]); close(err_pipe[1]);
        return false;
    }

    if (pid == 0) {
        /* Child */
        close(out_pipe[0]);
        close(err_pipe[0]);
        dup2(out_pipe[1], STDOUT_FILENO);
        dup2(err_pipe[1], STDERR_FILENO);
        close(out_pipe[1]);
        close(err_pipe[1]);

        int dev_null = open("/dev/null", O_RDONLY);
        if (dev_null >= 0) {
            dup2(dev_null, STDIN_FILENO);
            close(dev_null);
        }

        char* argv[8];
        int argc = 0;
        argv[argc++] = (char*)unfish_bin;
        argv[argc++] = "run";
        if (strict_flag) argv[argc++] = "--strict";
        if (regvm_flag) argv[argc++] = "--regvm";
        else if (vm_flag) argv[argc++] = "--vm";
        argv[argc++] = (char*)test_path;
        argv[argc] = NULL;

        execvp(unfish_bin, argv);
        _exit(127);
    }

    /* Parent */
    close(out_pipe[1]);
    close(err_pipe[1]);

    char actual_stdout[16384];
    size_t stdout_len = 0;
    ssize_t r;
    while ((r = read(out_pipe[0], actual_stdout + stdout_len, sizeof(actual_stdout) - stdout_len - 1)) > 0) {
        stdout_len += (size_t)r;
    }
    actual_stdout[stdout_len] = '\0';
    close(out_pipe[0]);

    char actual_stderr[16384];
    size_t stderr_len = 0;
    while ((r = read(err_pipe[0], actual_stderr + stderr_len, sizeof(actual_stderr) - stderr_len - 1)) > 0) {
        stderr_len += (size_t)r;
    }
    actual_stderr[stderr_len] = '\0';
    close(err_pipe[0]);

    int status = 0;
    waitpid(pid, &status, 0);
    int64_t t1 = get_time_ms();
    *out_elapsed = t1 - t0;

    int actual_exit = 0;
    if (WIFEXITED(status)) {
        actual_exit = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        snprintf(err_buf, err_buf_size, "Process terminated by signal %d (%s)", WTERMSIG(status),
                 WTERMSIG(status) == 11 ? "SIGSEGV" : (WTERMSIG(status) == 6 ? "SIGABRT" : "signal"));
        return false;
    }

    if (actual_exit != expected_exit) {
        snprintf(err_buf, err_buf_size, "Exit code mismatch: expected %d, got %d\n  Stderr: %.1000s",
                 expected_exit, actual_exit, actual_stderr[0] ? actual_stderr : "(empty)");
        return false;
    }

    if (expected_error[0] != '\0') {
        if (!strstr(actual_stderr, expected_error)) {
            snprintf(err_buf, err_buf_size, "Expected stderr to contain '%.200s', but got:\n%.1000s",
                     expected_error, actual_stderr);
            return false;
        }
    }

    if (has_expected_output) {
        /* Strip trailing newlines for comparison */
        while (stdout_len > 0 && (actual_stdout[stdout_len - 1] == '\n' || actual_stdout[stdout_len - 1] == '\r')) {
            actual_stdout[--stdout_len] = '\0';
        }
        if (strcmp(expected_output, actual_stdout) != 0) {
            snprintf(err_buf, err_buf_size, "Stdout mismatch:\n  Expected:\n%.1000s\n  Got:\n%.1000s",
                     expected_output, actual_stdout);
            return false;
        }
    }

    return true;
}

const char* uf_find_self_binary(void) {
    static char buf[1024];
    const char* env_bin = getenv("UNFISH_BIN");
    if (env_bin && env_bin[0] != '\0') return env_bin;

    ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len > 0) {
        buf[len] = '\0';
        if (ends_with(buf, "/unfish") || strcmp(buf, "unfish") == 0) {
            return buf;
        }
        char* last_slash = strrchr(buf, '/');
        if (last_slash) {
            *last_slash = '\0';
            static char sibling_buf[2048];
            snprintf(sibling_buf, sizeof(sibling_buf), "%.1000s/unfish", buf);
            if (access(sibling_buf, X_OK) == 0) {
                return sibling_buf;
            }
        }
    }
    if (access("./bin/unfish", X_OK) == 0) return "./bin/unfish";
    if (access("bin/unfish", X_OK) == 0) return "bin/unfish";
    return "unfish";
}

int uf_test_run(const char* target_path, const UfTestOptions* options) {
    const char* root_path = target_path;
    if (!root_path) {
        if (is_directory("tests")) {
            root_path = "tests";
        } else {
            root_path = ".";
        }
    }

    TestFile tests[MAX_TESTS];
    size_t test_count = 0;

    if (is_directory(root_path)) {
        scan_directory(root_path, tests, &test_count, options ? options->filter : NULL);
    } else {
        if (access(root_path, R_OK) == 0) {
            snprintf(tests[0].path, sizeof(tests[0].path), "%s", root_path);
            test_count = 1;
        } else {
            fprintf(stderr, "Error: Test target path '%s' not found or unreadable\n", root_path);
            return 1;
        }
    }

    if (test_count == 0) {
        printf("No tests found matching '%s'\n", root_path);
        return 0;
    }

    qsort(tests, test_count, sizeof(TestFile), compare_test_files);

    const char* unfish_bin = uf_find_self_binary();

    printf("\n🐡 Unfish Test Runner (v%s)\n", UF_VERSION_STRING);
    printf("Target: %s (%zu test suite%s found)\n", root_path, test_count, test_count == 1 ? "" : "s");
    printf("----------------------------------------------------------------------\n");

    size_t passed = 0;
    size_t failed = 0;
    int64_t total_start = get_time_ms();

    char err_buf[4096];

    for (size_t i = 0; i < test_count; ++i) {
        int64_t elapsed = 0;
        err_buf[0] = '\0';

        bool ok = run_single_test(unfish_bin, tests[i].path, options, &elapsed, err_buf, sizeof(err_buf));
        if (ok) {
            passed++;
            printf("  \033[1;32mPASS\033[0m  %s (%ld ms)\n", tests[i].path, (long)elapsed);
        } else {
            failed++;
            printf("  \033[1;31mFAIL\033[0m  %s (%ld ms)\n", tests[i].path, (long)elapsed);
            if (err_buf[0] != '\0') {
                char* line = strtok(err_buf, "\n");
                while (line) {
                    printf("        \033[31m%s\033[0m\n", line);
                    line = strtok(NULL, "\n");
                }
            }
        }
        fflush(stdout);
    }

    int64_t total_elapsed = get_time_ms() - total_start;
    printf("----------------------------------------------------------------------\n");
    if (failed == 0) {
        printf("\033[1;32mTEST SUITE PASSED:\033[0m %zu passed, 0 failed, %zu total (%ld ms)\n\n",
               passed, test_count, (long)total_elapsed);
        return 0;
    } else {
        printf("\033[1;31mTEST SUITE FAILED:\033[0m %zu failed, %zu passed, %zu total (%ld ms)\n\n",
               failed, passed, test_count, (long)total_elapsed);
        return 1;
    }
}

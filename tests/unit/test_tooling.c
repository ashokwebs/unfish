#include "../../src/tooling/uf_doc.h"
#include "../../src/tooling/uf_test_runner.h"
#include "../../src/tooling/uf_pkg.h"
#include "../../src/tooling/uf_learn.h"
#include "../../src/tooling/uf_playground.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>


/* Regression: in a file that opens directly with a documented declaration --
 * no separate module docstring -- the leading '##' block was consumed as the
 * file's description, so the first documented function in the file silently
 * lost its documentation. The generator computed whether the block was
 * attached to a declaration and then discarded the answer. */
static void test_doc_first_declaration_keeps_its_docstring(void) {
    const char* sample_code =
        "## Adds two numbers together.\n"
        "function add(a, b):\n"
        "    return a + b\n"
        "\n"
        "## Subtracts two numbers.\n"
        "function sub(a, b):\n"
        "    return a - b\n";

    char tmp_src[] = "/tmp/uf_doc_first_XXXXXX.unfish";
    int fd = mkstemps(tmp_src, 7);
    assert(fd >= 0);
    write(fd, sample_code, strlen(sample_code));
    close(fd);

    char tmp_out[] = "/tmp/uf_doc_first_out_XXXXXX.md";
    int out_fd = mkstemps(tmp_out, 3);
    assert(out_fd >= 0);
    close(out_fd);

    UfDocOptions opts;
    memset(&opts, 0, sizeof(opts));
    opts.output_path = tmp_out;
    opts.title = "First Decl";
    opts.format = UF_DOC_FORMAT_MARKDOWN;

    int rc = uf_doc_generate(tmp_src, &opts);
    assert(rc == 0);

    FILE* f = fopen(tmp_out, "r");
    assert(f != NULL);
    char buf[8192];
    size_t len = fread(buf, 1, sizeof(buf) - 1, f);
    buf[len] = '\0';
    fclose(f);

    /* Both docstrings must survive, each attached to its own function. */
    const char* add_entry = strstr(buf, "function add(");
    assert(add_entry != NULL);
    assert(strstr(add_entry, "Adds two numbers together.") != NULL);

    const char* sub_entry = strstr(buf, "function sub(");
    assert(sub_entry != NULL);
    assert(strstr(sub_entry, "Subtracts two numbers.") != NULL);

    /* And the first one must not have been hoisted above the contents list
     * as if it were the module's own description. */
    const char* toc = strstr(buf, "Table of Contents");
    if (toc != NULL) {
        size_t head_len = (size_t)(toc - buf);
        char head[4096];
        if (head_len >= sizeof(head)) head_len = sizeof(head) - 1;
        memcpy(head, buf, head_len);
        head[head_len] = '\0';
        assert(strstr(head, "Adds two numbers together.") == NULL);
    }

    remove(tmp_src);
    remove(tmp_out);
    printf("test_doc_first_declaration_keeps_its_docstring passed!\n");
}

static void test_doc_generation(void) {
    const char* sample_code =
        "## Sample Math Library\n"
        "## Provides geometric and arithmetic operations.\n"
        "\n"
        "## Represents a 2D geometric vector.\n"
        "struct Vector2:\n"
        "    x: Number\n"
        "    y: Number\n"
        "    ## Computes magnitude.\n"
        "    fn length(self):\n"
        "        return (self.x * self.x + self.y * self.y)\n"
        "\n"
        "## Shape enumeration.\n"
        "enum Shape:\n"
        "    Circle(radius: Number)\n"
        "    Rectangle(w: Number, h: Number)\n"
        "\n"
        "## Adds two numbers together.\n"
        "function add(a: Number, b: Number): Number:\n"
        "    return a + b\n";

    char tmp_src[] = "/tmp/uf_doc_sample_XXXXXX.unfish";
    int fd = mkstemps(tmp_src, 7);
    assert(fd >= 0);
    write(fd, sample_code, strlen(sample_code));
    close(fd);

    char tmp_out[] = "/tmp/uf_doc_out_XXXXXX.md";
    int out_fd = mkstemps(tmp_out, 3);
    assert(out_fd >= 0);
    close(out_fd);

    UfDocOptions opts;
    memset(&opts, 0, sizeof(opts));
    opts.output_path = tmp_out;
    opts.title = "MathLib API";
    opts.format = UF_DOC_FORMAT_MARKDOWN;

    int rc = uf_doc_generate(tmp_src, &opts);
    assert(rc == 0);

    FILE* f = fopen(tmp_out, "r");
    assert(f != NULL);
    char buf[8192];
    size_t len = fread(buf, 1, sizeof(buf) - 1, f);
    buf[len] = '\0';
    fclose(f);

    assert(strstr(buf, "# MathLib API") != NULL);
    assert(strstr(buf, "Sample Math Library") != NULL);
    assert(strstr(buf, "struct Vector2") != NULL);
    assert(strstr(buf, "Represents a 2D geometric vector.") != NULL);
    assert(strstr(buf, "enum Shape") != NULL);
    assert(strstr(buf, "Circle") != NULL);
    assert(strstr(buf, "Rectangle") != NULL);
    assert(strstr(buf, "function add(") != NULL);
    assert(strstr(buf, "Adds two numbers together.") != NULL);

    /* Test HTML format */
    char tmp_html[] = "/tmp/uf_doc_out_XXXXXX.html";
    int html_fd = mkstemps(tmp_html, 5);
    assert(html_fd >= 0);
    close(html_fd);

    opts.output_path = tmp_html;
    opts.format = UF_DOC_FORMAT_HTML;
    rc = uf_doc_generate(tmp_src, &opts);
    assert(rc == 0);

    FILE* hf = fopen(tmp_html, "r");
    assert(hf != NULL);
    len = fread(buf, 1, sizeof(buf) - 1, hf);
    buf[len] = '\0';
    fclose(hf);

    assert(strstr(buf, "<!DOCTYPE html>") != NULL);
    assert(strstr(buf, "<title>MathLib API</title>") != NULL);
    assert(strstr(buf, "<h2>struct <code>Vector2</code></h2>") != NULL);

    unlink(tmp_src);
    unlink(tmp_out);
    unlink(tmp_html);
    printf("test_doc_generation passed!\n");
}

static void test_test_runner(void) {
    const char* passing_test =
        "# expect: 42\n"
        "say 40 + 2\n";

    char tmp_pass[] = "/tmp/uf_test_pass_XXXXXX.unfish";
    int fd = mkstemps(tmp_pass, 7);
    assert(fd >= 0);
    write(fd, passing_test, strlen(passing_test));
    close(fd);

    UfTestOptions opts;
    memset(&opts, 0, sizeof(opts));
    int rc = uf_test_run(tmp_pass, &opts);
    assert(rc == 0);

    const char* failing_test =
        "# expect: 99\n"
        "say 100\n";

    char tmp_fail[] = "/tmp/uf_test_fail_XXXXXX.unfish";
    fd = mkstemps(tmp_fail, 7);
    assert(fd >= 0);
    write(fd, failing_test, strlen(failing_test));
    close(fd);

    rc = uf_test_run(tmp_fail, &opts);
    assert(rc == 1);

    unlink(tmp_pass);
    unlink(tmp_fail);
    printf("test_test_runner passed!\n");
}

static void test_pkg_manager(void) {
    char orig_cwd[1024];
    char* cwd_res = getcwd(orig_cwd, sizeof(orig_cwd));
    assert(cwd_res != NULL);

    char tmp_dir[] = "/tmp/uf_pkg_unit_XXXXXX";
    char* dname = mkdtemp(tmp_dir);
    assert(dname != NULL);

    int ch = chdir(tmp_dir);
    assert(ch == 0);

    int rc = uf_pkg_init("demo_pkg");
    assert(rc == 0);

    assert(access("unfish.toml", F_OK) == 0);
    assert(access("src/main.unfish", F_OK) == 0);
    assert(access("tests/main_test.unfish", F_OK) == 0);

    rc = uf_pkg_check();
    assert(rc == 0);

    /* The scaffold has to actually pass its own test command. It previously
     * did not: the generated test imported the testing module as
     * `from "testing" import ...`, but a module name is a bare identifier and
     * the quoted form is a syntax error -- so every freshly created package
     * failed `unfish pkg test` on the very first run. Running the real thing
     * here also covers stdlib module resolution from a directory that is not
     * the Unfish source tree. */
    rc = uf_pkg_test();
    assert(rc == 0);

    /* Second init in same dir should fail */
    rc = uf_pkg_init("duplicate");
    assert(rc == 1);

    ch = chdir(orig_cwd);
    assert(ch == 0);

    /* Clean up */
    char rm_cmd[1024];
    snprintf(rm_cmd, sizeof(rm_cmd), "rm -rf %s", tmp_dir);
    int sys_rc = system(rm_cmd);
    (void)sys_rc;

    printf("test_pkg_manager passed!\n");
}

static void test_learn_tutorial(void) {
    printf("Testing interactive tutorial list...\n");
    uf_learn_list();
    printf("test_learn_tutorial passed!\n");
}

static void test_playground_server(void) {
    printf("Testing Web Playground server...\n");
    assert(access("web/index.html", R_OK) == 0);
    assert(access("web/styles.css", R_OK) == 0);
    assert(access("web/playground.js", R_OK) == 0);
    assert(access("web/unfish_engine.js", R_OK) == 0);

    pid_t pid = fork();
    assert(pid >= 0);
    if (pid == 0) {
        UfPlaygroundOptions opts;
        memset(&opts, 0, sizeof(opts));
        opts.port = 9188;
        opts.web_root = "web";
        opts.open_browser = false;
        int rc = uf_playground_start(&opts);
        _exit(rc);
    }

    /* Wait briefly for server to bind */
    usleep(150000);

    /* Send shutdown request via socket */
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    assert(sock >= 0);
    struct sockaddr_in saddr;
    memset(&saddr, 0, sizeof(saddr));
    saddr.sin_family = AF_INET;
    saddr.sin_port = htons(9188);
    inet_pton(AF_INET, "127.0.0.1", &saddr.sin_addr);

    int conn = -1;
    for (int retry = 0; retry < 20; ++retry) {
        conn = connect(sock, (struct sockaddr*)&saddr, sizeof(saddr));
        if (conn == 0) break;
        usleep(50000);
    }
    assert(conn == 0);

    /* Test /api/blocks-import */
    const char* blocks_json = "{\"schema\":\"unfish_blocks_v1\",\"statements\":[{\"kind\":\"let\",\"name\":\"x\",\"type_ann\":null,\"pattern\":null,\"init\":{\"kind\":\"literal_number\",\"value\":42}}]}";
    char post_req[1024];
    snprintf(post_req, sizeof(post_req),
             "POST /api/blocks-import HTTP/1.1\r\nHost: localhost\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n%s",
             strlen(blocks_json), blocks_json);
    ssize_t post_sent = write(sock, post_req, strlen(post_req));
    assert(post_sent > 0);

    char post_resp[2048];
    size_t post_recvd = 0;
    while (post_recvd < sizeof(post_resp) - 1) {
        ssize_t n = read(sock, post_resp + post_recvd, sizeof(post_resp) - 1 - post_recvd);
        if (n <= 0) break;
        post_recvd += (size_t)n;
    }
    post_resp[post_recvd] = '\0';
    assert(strstr(post_resp, "200 OK") != NULL);
    assert(strstr(post_resp, "let x = 42") != NULL);
    close(sock);

    /* Open new connection for shutdown */
    sock = socket(AF_INET, SOCK_STREAM, 0);
    assert(sock >= 0);
    int shut_conn = connect(sock, (struct sockaddr*)&saddr, sizeof(saddr));
    assert(shut_conn == 0);

    const char* req = "GET /api/shutdown HTTP/1.1\r\nHost: localhost\r\n\r\n";
    ssize_t sent = write(sock, req, strlen(req));
    assert(sent > 0);

    char resp[1024];
    size_t total_recvd = 0;
    while (total_recvd < sizeof(resp) - 1) {
        ssize_t n = read(sock, resp + total_recvd, sizeof(resp) - 1 - total_recvd);
        if (n <= 0) break;
        total_recvd += (size_t)n;
        resp[total_recvd] = '\0';
        if (strstr(resp, "stopping") != NULL) break;
    }
    resp[total_recvd] = '\0';
    assert(strstr(resp, "stopping") != NULL);
    close(sock);

    int status = 0;
    waitpid(pid, &status, 0);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);

    printf("test_playground_server passed!\n");
}

int main(void) {
    printf("Running tooling unit tests...\n");
    test_doc_generation();
    test_doc_first_declaration_keeps_its_docstring();
    test_test_runner();
    test_pkg_manager();
    test_learn_tutorial();
    test_playground_server();
    printf("All tooling unit tests passed successfully!\n");
    return 0;
}

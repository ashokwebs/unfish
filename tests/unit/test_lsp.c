#include "../../src/lsp/uf_lsp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>

static void send_lsp_msg(FILE* out, const char* json) {
    fprintf(out, "Content-Length: %zu\r\n\r\n%s", strlen(json), json);
    fflush(out);
}

static void test_lsp_handshake(void) {
    FILE* in_file = tmpfile();
    FILE* out_file = tmpfile();
    assert(in_file != NULL && out_file != NULL);

    send_lsp_msg(in_file, "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{}}");
    send_lsp_msg(in_file, "{\"jsonrpc\":\"2.0\",\"method\":\"initialized\",\"params\":{}}");
    send_lsp_msg(in_file, "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"shutdown\",\"params\":{}}");
    send_lsp_msg(in_file, "{\"jsonrpc\":\"2.0\",\"method\":\"exit\",\"params\":{}}");
    rewind(in_file);

    int rc = uf_lsp_run(in_file, out_file);
    assert(rc == 0);

    rewind(out_file);
    char buf[4096];
    size_t len = fread(buf, 1, sizeof(buf) - 1, out_file);
    buf[len] = '\0';

    assert(strstr(buf, "\"hoverProvider\":true") != NULL);
    assert(strstr(buf, "\"definitionProvider\":true") != NULL);
    assert(strstr(buf, "\"completionProvider\":") != NULL);
    assert(strstr(buf, "\"documentFormattingProvider\":true") != NULL);
    assert(strstr(buf, "\"id\":2") != NULL);

    fclose(in_file);
    fclose(out_file);
    printf("test_lsp_handshake passed!\n");
}

static void test_lsp_diagnostics(void) {
    FILE* in_file = tmpfile();
    FILE* out_file = tmpfile();
    assert(in_file != NULL && out_file != NULL);

    /* Open document with syntax error: let = 5 */
    send_lsp_msg(in_file,
        "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didOpen\",\"params\":{"
        "\"textDocument\":{\"uri\":\"file:///test.unfish\",\"text\":\"let = 5\\n\"}}}");
    send_lsp_msg(in_file, "{\"jsonrpc\":\"2.0\",\"method\":\"exit\",\"params\":{}}");
    rewind(in_file);

    int rc = uf_lsp_run(in_file, out_file);
    assert(rc == 0);

    rewind(out_file);
    char buf[4096];
    size_t len = fread(buf, 1, sizeof(buf) - 1, out_file);
    buf[len] = '\0';

    assert(strstr(buf, "textDocument/publishDiagnostics") != NULL);
    assert(strstr(buf, "Expected variable name") != NULL);

    fclose(in_file);
    fclose(out_file);
    printf("test_lsp_diagnostics passed!\n");
}

static void test_lsp_hover_and_completion(void) {
    FILE* in_file = tmpfile();
    FILE* out_file = tmpfile();
    assert(in_file != NULL && out_file != NULL);

    /* Open document */
    send_lsp_msg(in_file,
        "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didOpen\",\"params\":{"
        "\"textDocument\":{\"uri\":\"file:///demo.unfish\",\"text\":\"say 42\\n\"}}}");
    /* Hover over 'say' at line 0, char 1 */
    send_lsp_msg(in_file,
        "{\"jsonrpc\":\"2.0\",\"id\":10,\"method\":\"textDocument/hover\",\"params\":{"
        "\"textDocument\":{\"uri\":\"file:///demo.unfish\"},\"position\":{\"line\":0,\"character\":1}}}");
    /* Completion */
    send_lsp_msg(in_file,
        "{\"jsonrpc\":\"2.0\",\"id\":20,\"method\":\"textDocument/completion\",\"params\":{"
        "\"textDocument\":{\"uri\":\"file:///demo.unfish\"},\"position\":{\"line\":0,\"character\":3}}}");
    send_lsp_msg(in_file, "{\"jsonrpc\":\"2.0\",\"method\":\"exit\",\"params\":{}}");
    rewind(in_file);

    int rc = uf_lsp_run(in_file, out_file);
    assert(rc == 0);

    rewind(out_file);
    char buf[8192];
    size_t len = fread(buf, 1, sizeof(buf) - 1, out_file);
    buf[len] = '\0';

    assert(strstr(buf, "**function** `say") != NULL);
    assert(strstr(buf, "\"label\":\"function\"") != NULL);
    assert(strstr(buf, "\"label\":\"let\"") != NULL);

    fclose(in_file);
    fclose(out_file);
    printf("test_lsp_hover_and_completion passed!\n");
}

static void test_lsp_formatting(void) {
    FILE* in_file = tmpfile();
    FILE* out_file = tmpfile();
    assert(in_file != NULL && out_file != NULL);

    /* Open unformatted doc */
    send_lsp_msg(in_file,
        "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didOpen\",\"params\":{"
        "\"textDocument\":{\"uri\":\"file:///fmt.unfish\",\"text\":\"let x=10\\nsay x\\n\"}}}");
    /* Request formatting */
    send_lsp_msg(in_file,
        "{\"jsonrpc\":\"2.0\",\"id\":30,\"method\":\"textDocument/formatting\",\"params\":{"
        "\"textDocument\":{\"uri\":\"file:///fmt.unfish\"},\"options\":{}}}");
    send_lsp_msg(in_file, "{\"jsonrpc\":\"2.0\",\"method\":\"exit\",\"params\":{}}");
    rewind(in_file);

    int rc = uf_lsp_run(in_file, out_file);
    assert(rc == 0);

    rewind(out_file);
    char buf[4096];
    size_t len = fread(buf, 1, sizeof(buf) - 1, out_file);
    buf[len] = '\0';

    assert(strstr(buf, "\"id\":30") != NULL);
    assert(strstr(buf, "\"newText\":") != NULL);
    assert(strstr(buf, "let x = 10") != NULL);

    fclose(in_file);
    fclose(out_file);
    printf("test_lsp_formatting passed!\n");
}

int main(void) {
    printf("Running Language Server Protocol (LSP) tests...\n");
    test_lsp_handshake();
    test_lsp_diagnostics();
    test_lsp_hover_and_completion();
    test_lsp_formatting();
    printf("All LSP tests passed successfully!\n");
    return 0;
}

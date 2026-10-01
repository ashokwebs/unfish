#include "uf_playground.h"
#include "uf_test_runner.h"
#include "../common/uf_string.h"

#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>

static volatile sig_atomic_t g_server_running = 1;

static void sigint_handler(int sig) {
    (void)sig;
    g_server_running = 0;
}

static const char* get_mime_type(const char* path) {
    const char* dot = strrchr(path, '.');
    if (!dot) return "application/octet-stream";
    if (strcmp(dot, ".html") == 0 || strcmp(dot, ".htm") == 0) return "text/html; charset=utf-8";
    if (strcmp(dot, ".css") == 0) return "text/css; charset=utf-8";
    if (strcmp(dot, ".js") == 0) return "application/javascript; charset=utf-8";
    if (strcmp(dot, ".json") == 0) return "application/json";
    if (strcmp(dot, ".svg") == 0) return "image/svg+xml";
    if (strcmp(dot, ".png") == 0) return "image/png";
    if (strcmp(dot, ".ico") == 0) return "image/x-icon";
    if (strcmp(dot, ".txt") == 0 || strcmp(dot, ".unfish") == 0) return "text/plain; charset=utf-8";
    return "application/octet-stream";
}

static void send_response(int client_fd, int status_code, const char* status_text,
                          const char* content_type, const char* body, size_t body_len) {
    char header[512];
    int hlen = snprintf(header, sizeof(header),
                        "HTTP/1.1 %d %s\r\n"
                        "Content-Type: %s\r\n"
                        "Content-Length: %zu\r\n"
                        "Access-Control-Allow-Origin: *\r\n"
                        "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
                        "Access-Control-Allow-Headers: Content-Type\r\n"
                        "Connection: close\r\n\r\n",
                        status_code, status_text, content_type, body_len);
    if (hlen > 0) {
        ssize_t written = 0;
        while (written < hlen) {
            ssize_t n = write(client_fd, header + written, (size_t)(hlen - written));
            if (n <= 0) return;
            written += n;
        }
    }
    if (body && body_len > 0) {
        ssize_t written = 0;
        while ((size_t)written < body_len) {
            ssize_t n = write(client_fd, body + written, body_len - (size_t)written);
            if (n <= 0) return;
            written += n;
        }
    }
}

static void json_escape_append(UfStrBuf* buf, const char* str) {
    if (!str) return;
    for (size_t i = 0; str[i] != '\0'; ++i) {
        unsigned char c = (unsigned char)str[i];
        switch (c) {
            case '"':  uf_strbuf_append(buf, "\\\""); break;
            case '\\': uf_strbuf_append(buf, "\\\\"); break;
            case '\b': uf_strbuf_append(buf, "\\b"); break;
            case '\f': uf_strbuf_append(buf, "\\f"); break;
            case '\n': uf_strbuf_append(buf, "\\n"); break;
            case '\r': uf_strbuf_append(buf, "\\r"); break;
            case '\t': uf_strbuf_append(buf, "\\t"); break;
            default:
                if (c < 32) {
                    char hex[8];
                    snprintf(hex, sizeof(hex), "\\u%04x", c);
                    uf_strbuf_append(buf, hex);
                } else {
                    uf_strbuf_append_char(buf, (char)c);
                }
                break;
        }
    }
}

static int execute_command_capture(const char* const argv[], char** out_str, char** err_str) {
    int out_pipe[2];
    int err_pipe[2];
    if (pipe(out_pipe) != 0 || pipe(err_pipe) != 0) {
        return -1;
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(out_pipe[0]); close(out_pipe[1]);
        close(err_pipe[0]); close(err_pipe[1]);
        return -1;
    }

    if (pid == 0) {
        /* Child */
        close(out_pipe[0]);
        close(err_pipe[0]);
        dup2(out_pipe[1], STDOUT_FILENO);
        dup2(err_pipe[1], STDERR_FILENO);
        close(out_pipe[1]);
        close(err_pipe[1]);

        int devnull = open("/dev/null", O_RDONLY);
        if (devnull >= 0) {
            dup2(devnull, STDIN_FILENO);
            close(devnull);
        }

        execvp(argv[0], (char* const*)argv);
        _exit(127);
    }

    /* Parent */
    close(out_pipe[1]);
    close(err_pipe[1]);

    UfStrBuf out_buf, err_buf;
    uf_strbuf_init(&out_buf);
    uf_strbuf_init(&err_buf);

    char read_buf[4096];
    ssize_t n;
    while ((n = read(out_pipe[0], read_buf, sizeof(read_buf))) > 0) {
        uf_strbuf_append_len(&out_buf, read_buf, (size_t)n);
    }
    close(out_pipe[0]);

    while ((n = read(err_pipe[0], read_buf, sizeof(read_buf))) > 0) {
        uf_strbuf_append_len(&err_buf, read_buf, (size_t)n);
    }
    close(err_pipe[0]);

    int status = 0;
    waitpid(pid, &status, 0);

    *out_str = uf_strbuf_detach(&out_buf);
    *err_str = uf_strbuf_detach(&err_buf);

    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    return 1;
}

static void handle_api_run(int client_fd, const char* body, int engine_mode) {
    char tmp_path[] = "/tmp/unfish_play_XXXXXX";
    int fd = mkstemp(tmp_path);
    if (fd < 0) {
        send_response(client_fd, 500, "Internal Error", "application/json", "{\"error\":\"Failed to create temp file\"}", 37);
        return;
    }

    size_t body_len = strlen(body);
    ssize_t written = write(fd, body, body_len);
    (void)written;
    close(fd);

    const char* self_bin = uf_find_self_binary();
    const char* argv[7];
    int argc = 0;
    argv[argc++] = self_bin;
    argv[argc++] = "run";
    if (engine_mode == 1) {
        argv[argc++] = "--vm";
    } else if (engine_mode == 2) {
        argv[argc++] = "--regvm";
    } else if (engine_mode == 3) {
        argv[argc++] = "--wasm";
    }
    argv[argc++] = tmp_path;
    argv[argc] = NULL;

    char* stdout_res = NULL;
    char* stderr_res = NULL;
    int exit_code = execute_command_capture(argv, &stdout_res, &stderr_res);
    unlink(tmp_path);

    UfStrBuf resp;
    uf_strbuf_init(&resp);
    uf_strbuf_append(&resp, "{\"exit_code\":");
    char code_str[16];
    snprintf(code_str, sizeof(code_str), "%d", exit_code);
    uf_strbuf_append(&resp, code_str);
    uf_strbuf_append(&resp, ",\"stdout\":\"");
    json_escape_append(&resp, stdout_res ? stdout_res : "");
    uf_strbuf_append(&resp, "\",\"stderr\":\"");
    json_escape_append(&resp, stderr_res ? stderr_res : "");
    uf_strbuf_append(&resp, "\"}");

    char* final_json = uf_strbuf_detach(&resp);
    send_response(client_fd, 200, "OK", "application/json", final_json, strlen(final_json));

    free(stdout_res);
    free(stderr_res);
    free(final_json);
}

static void handle_api_command(int client_fd, const char* subcmd, const char* body) {
    char tmp_path[] = "/tmp/unfish_play_XXXXXX";
    int fd = mkstemp(tmp_path);
    if (fd < 0) {
        send_response(client_fd, 500, "Internal Error", "application/json", "{\"error\":\"Failed to create temp file\"}", 37);
        return;
    }

    size_t body_len = strlen(body);
    ssize_t written = write(fd, body, body_len);
    (void)written;
    close(fd);

    const char* self_bin = uf_find_self_binary();
    const char* argv[5];
    argv[0] = self_bin;
    argv[1] = subcmd;
    argv[2] = tmp_path;
    argv[3] = NULL;

    char* stdout_res = NULL;
    char* stderr_res = NULL;
    int exit_code = execute_command_capture(argv, &stdout_res, &stderr_res);
    unlink(tmp_path);

    UfStrBuf resp;
    uf_strbuf_init(&resp);
    uf_strbuf_append(&resp, "{\"exit_code\":");
    char code_str[16];
    snprintf(code_str, sizeof(code_str), "%d", exit_code);
    uf_strbuf_append(&resp, code_str);
    uf_strbuf_append(&resp, ",\"stdout\":\"");
    json_escape_append(&resp, stdout_res ? stdout_res : "");
    uf_strbuf_append(&resp, "\",\"stderr\":\"");
    json_escape_append(&resp, stderr_res ? stderr_res : "");
    uf_strbuf_append(&resp, "\"}");

    char* final_json = uf_strbuf_detach(&resp);
    send_response(client_fd, 200, "OK", "application/json", final_json, strlen(final_json));

    free(stdout_res);
    free(stderr_res);
    free(final_json);
}

static void handle_static_file(int client_fd, const char* web_root, const char* url_path) {
    if (strstr(url_path, "..")) {
        send_response(client_fd, 403, "Forbidden", "text/plain", "Forbidden", 9);
        return;
    }

    char file_path[1024];
    if (strcmp(url_path, "/") == 0 || strcmp(url_path, "/index.html") == 0) {
        snprintf(file_path, sizeof(file_path), "%s/index.html", web_root);
    } else if (strcmp(url_path, "/studio") == 0 || strcmp(url_path, "/studio/") == 0 || strcmp(url_path, "/studio.html") == 0) {
        snprintf(file_path, sizeof(file_path), "%s/studio.html", web_root);
    } else if (strcmp(url_path, "/learn") == 0 || strcmp(url_path, "/learn/") == 0 || strcmp(url_path, "/learn.html") == 0) {
        snprintf(file_path, sizeof(file_path), "%s/learn.html", web_root);
    } else if (strcmp(url_path, "/playground") == 0 || strcmp(url_path, "/playground/") == 0 || strcmp(url_path, "/playground.html") == 0) {
        snprintf(file_path, sizeof(file_path), "%s/playground.html", web_root);
    } else {
        snprintf(file_path, sizeof(file_path), "%s%s", web_root, url_path);
    }

    FILE* f = fopen(file_path, "rb");
    if (!f && !strrchr(url_path, '.')) {
        char try_html[1024];
        snprintf(try_html, sizeof(try_html), "%s%s.html", web_root, url_path);
        f = fopen(try_html, "rb");
        if (f) {
            strncpy(file_path, try_html, sizeof(file_path) - 1);
            file_path[sizeof(file_path) - 1] = '\0';
        }
    }

    if (!f) {
        send_response(client_fd, 404, "Not Found", "text/plain", "File Not Found", 14);
        return;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size < 0) {
        fclose(f);
        send_response(client_fd, 500, "Error", "text/plain", "Error reading file", 18);
        return;
    }

    char* content = (char*)malloc((size_t)size + 1);
    if (!content) {
        fclose(f);
        send_response(client_fd, 500, "Error", "text/plain", "Out of memory", 13);
        return;
    }

    size_t read_bytes = fread(content, 1, (size_t)size, f);
    fclose(f);
    content[read_bytes] = '\0';

    const char* mime = get_mime_type(file_path);
    send_response(client_fd, 200, "OK", mime, content, read_bytes);
    free(content);
}

int uf_playground_start(const UfPlaygroundOptions* options) {
    int port = options->port > 0 ? options->port : 8080;
    const char* web_root = options->web_root ? options->web_root : "web";

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        fprintf(stderr, "Error: Could not create server socket: %s\n", strerror(errno));
        return 1;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons((uint16_t)port);

    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        fprintf(stderr, "Error: Failed to bind to port %d: %s\n", port, strerror(errno));
        close(server_fd);
        return 1;
    }

    if (listen(server_fd, 16) < 0) {
        fprintf(stderr, "Error: Failed to listen on socket: %s\n", strerror(errno));
        close(server_fd);
        return 1;
    }

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sigint_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0; /* No SA_RESTART so accept returns EINTR */
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    printf("\n🐡 Unfish Web Playground is live!\n");
    printf("   URL:       \033[1;36mhttp://localhost:%d\033[0m\n", port);
    printf("   Web Root:  %s\n", web_root);
    printf("   Press \033[33mCtrl+C\033[0m to stop server.\n\n");

    if (options->open_browser) {
        char open_cmd[512];
        snprintf(open_cmd, sizeof(open_cmd), "xdg-open http://localhost:%d >/dev/null 2>&1 || open http://localhost:%d >/dev/null 2>&1 &", port, port);
        int sys_ret = system(open_cmd);
        (void)sys_ret;
    }

    while (g_server_running) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
        if (client_fd < 0) {
            if (errno == EINTR) break;
            continue;
        }

        /* Set read timeout on client socket */
        struct timeval tv;
        tv.tv_sec = 5;
        tv.tv_usec = 0;
        setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));

        char req_buf[8192];
        ssize_t req_bytes = read(client_fd, req_buf, sizeof(req_buf) - 1);
        if (req_bytes <= 0) {
            close(client_fd);
            continue;
        }
        req_buf[req_bytes] = '\0';

        char method[16];
        char url[512];
        method[0] = '\0';
        url[0] = '\0';
        if (sscanf(req_buf, "%15s %511s", method, url) < 2) {
            close(client_fd);
            continue;
        }

        /* Find HTTP body */
        char* body_start = strstr(req_buf, "\r\n\r\n");
        if (body_start) {
            body_start += 4;
        } else {
            body_start = "";
        }

        if (strcmp(method, "OPTIONS") == 0) {
            send_response(client_fd, 204, "No Content", "text/plain", "", 0);
        } else if (strcmp(url, "/api/run") == 0 && strcmp(method, "POST") == 0) {
            handle_api_run(client_fd, body_start, 0);
        } else if (strcmp(url, "/api/run-vm") == 0 && strcmp(method, "POST") == 0) {
            handle_api_run(client_fd, body_start, 1);
        } else if (strcmp(url, "/api/run-regvm") == 0 && strcmp(method, "POST") == 0) {
            handle_api_run(client_fd, body_start, 2);
        } else if (strcmp(url, "/api/run-wasm") == 0 && strcmp(method, "POST") == 0) {
            handle_api_run(client_fd, body_start, 3);
        } else if (strcmp(url, "/api/blocks") == 0 && strcmp(method, "POST") == 0) {
            handle_api_command(client_fd, "blocks-export", body_start);
        } else if (strcmp(url, "/api/blocks-import") == 0 && strcmp(method, "POST") == 0) {
            handle_api_command(client_fd, "blocks-import", body_start);
        } else if (strcmp(url, "/api/ast") == 0 && strcmp(method, "POST") == 0) {
            handle_api_command(client_fd, "ast", body_start);
        } else if (strcmp(url, "/api/tokens") == 0 && strcmp(method, "POST") == 0) {
            handle_api_command(client_fd, "tokens", body_start);
        } else if (strcmp(url, "/api/disasm") == 0 && strcmp(method, "POST") == 0) {
            handle_api_command(client_fd, "disasm", body_start);
        } else if (strcmp(url, "/api/check") == 0 && strcmp(method, "POST") == 0) {
            handle_api_command(client_fd, "check", body_start);
        } else if (strcmp(url, "/api/format") == 0 && strcmp(method, "POST") == 0) {
            handle_api_command(client_fd, "format", body_start);
        } else if (strcmp(url, "/api/status") == 0 && strcmp(method, "GET") == 0) {
            const char* status_body = "{\"status\":\"ok\",\"version\":\"" UF_VERSION_STRING "\",\"features\":[\"ast\",\"vm\",\"regvm\",\"wasm\",\"disasm\",\"tokens\",\"blocks\",\"check\"]}";
            send_response(client_fd, 200, "OK", "application/json", status_body, strlen(status_body));
        } else if (strcmp(url, "/api/shutdown") == 0) {
            const char* shut_body = "{\"status\":\"stopping\"}";
            send_response(client_fd, 200, "OK", "application/json", shut_body, strlen(shut_body));
            close(client_fd);
            g_server_running = 0;
            break;
        } else {
            handle_static_file(client_fd, web_root, url);
        }

        close(client_fd);
    }

    close(server_fd);
    printf("\n🐡 Unfish Web Playground server stopped cleanly.\n");
    return 0;
}

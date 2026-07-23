// test_net.c — Phase 2 tests for vv_net: HTTP transport + JSON adapter.
//
// All tests run against a LOCAL fixture HTTP server (POSIX sockets) — no
// dependency on the public internet. The server is started in a background
// thread and serves canned responses for various endpoints.
#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L
#include "verve/verve.h"
#include "vv_test.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

// ===========================================================================
//  Local HTTP fixture server
// ===========================================================================

typedef struct {
    int         port;
    int         listen_fd;
    int         client_fd;
    pthread_t   thread;
    atomic_int  stop;   // accessed from both the server thread and srv_stop
    vv_Net     *net;
} TestServer;

// Build a complete HTTP/1.1 response.
static void http_respond(int fd, int status, const char *status_text,
                         const char *content_type,
                         const void *body, size_t body_len) {
    char header[512];
    int hlen = snprintf(header, sizeof(header),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n"
        "\r\n",
        status, status_text, content_type, body_len);
    send(fd, header, (size_t)hlen, MSG_NOSIGNAL);
    if (body && body_len > 0)
        send(fd, body, body_len, MSG_NOSIGNAL);
}

// Read the full HTTP request (headers + body) into a buffer.
static void read_request(int fd, char *buf, size_t cap, size_t *out_len) {
    size_t total = 0;
    // Read until we have the full header (look for \r\n\r\n anywhere).
    while (total < cap - 1) {
        ssize_t n = read(fd, buf + total, cap - 1 - total);
        if (n <= 0) break;
        total += (size_t)n;
        buf[total] = '\0';
        // Check for end of headers anywhere in the buffer.
        if (memmem(buf, total, "\r\n\r\n", 4)) break;
    }
    buf[total] = '\0';
    *out_len = total;

    // Parse Content-Length and read remaining body.
    char *cl = strstr(buf, "Content-Length:");
    if (cl) {
        long len = strtol(cl + 15, NULL, 10);
        if (len > 0) {
            char *hdr_end = strstr(buf, "\r\n\r\n");
            if (hdr_end) {
                hdr_end += 4;
                size_t body_received = total - (size_t)(hdr_end - buf);
                while ((long)body_received < len && total < cap - 1) {
                    ssize_t n = read(fd, buf + total, cap - 1 - total);
                    if (n <= 0) break;
                    total += (size_t)n;
                    buf[total] = '\0';
                    body_received += (size_t)n;
                }
            }
        }
    }
    *out_len = total;
}

static void *server_thread(void *arg) {
    TestServer *srv = arg;
    while (!srv->stop) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(srv->listen_fd, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd < 0) continue;

        char buf[65536];
        size_t blen;
        read_request(client_fd, buf, sizeof(buf), &blen);

        // Route based on the request path.
        // Parse request line: METHOD PATH HTTP/VERSION
        char *line_end = strstr(buf, "\r\n");
        if (!line_end) { close(client_fd); continue; }
        char req_line[256];
        size_t line_len = (size_t)(line_end - buf);
        if (line_len >= sizeof(req_line)) line_len = sizeof(req_line) - 1;
        memcpy(req_line, buf, line_len);
        req_line[line_len] = '\0';

        char *path_start = strchr(req_line, ' ');
        if (!path_start) { close(client_fd); continue; }
        *path_start++ = '\0';
        char *path_end = strchr(path_start, ' ');
        if (!path_end) { close(client_fd); continue; }
        *path_end = '\0';
        char *req_path = path_start;

        if (strcmp(req_path, "/hello") == 0) {
            http_respond(client_fd, 200, "OK", "text/plain",
                         "Hello, Verve!", 13);
        } else if (strcmp(req_path, "/echo") == 0) {
            // Echo the request body back.
            char *body_ptr = strstr(buf, "\r\n\r\n");
            if (body_ptr) body_ptr += 4;
            else body_ptr = "";
            size_t elen = strlen(body_ptr);
            http_respond(client_fd, 200, "OK", "text/plain", body_ptr, elen);
        } else if (strcmp(req_path, "/headers") == 0) {
            // Return request headers as JSON.
            char resp[4096];
            int off = 0;
            char *hdr = strstr(buf, "X-Custom:");
            if (hdr) {
                char *val = hdr + 10;
                char *end = strstr(val, "\r\n");
                if (end) *end = '\0';
                while (*val == ' ') val++;
                off = snprintf(resp, sizeof(resp), "{\"X-Custom\":\"%s\"}", val);
            } else {
                off = snprintf(resp, sizeof(resp), "{\"X-Custom\":null}");
            }
            http_respond(client_fd, 200, "OK", "application/json",
                         resp, (size_t)off);
        } else if (strcmp(req_path, "/binary") == 0) {
            // Return binary data with NUL bytes.
            uint8_t bin[256];
            for (int i = 0; i < 256; i++) bin[i] = (uint8_t)i;
            http_respond(client_fd, 200, "OK", "application/octet-stream",
                         bin, 256);
        } else if (strcmp(req_path, "/notfound") == 0) {
            http_respond(client_fd, 404, "Not Found", "text/plain",
                         "Not Found", 9);
        } else if (strcmp(req_path, "/error") == 0) {
            http_respond(client_fd, 500, "Internal Server Error", "text/plain",
                         "Server Error", 12);
        } else if (strcmp(req_path, "/slow") == 0) {
            // Sleep 500ms then respond.
            usleep(500000);
            http_respond(client_fd, 200, "OK", "text/plain",
                         "slow", 4);
        } else if (strcmp(req_path, "/big") == 0) {
            // Return a large body (2 MiB) to trigger size limit.
            size_t big_len = 2 * 1024 * 1024;
            char *big = malloc(big_len);
            memset(big, 'X', big_len);
            http_respond(client_fd, 200, "OK", "text/plain", big, big_len);
            free(big);
        } else if (strcmp(req_path, "/json") == 0) {
            const char *json = "{\"name\":\"verve\",\"version\":42,\"active\":true,\"items\":[1,2,3]}";
            http_respond(client_fd, 200, "OK", "application/json",
                         json, strlen(json));
        } else if (strcmp(req_path, "/badjson") == 0) {
            const char *json = "{not valid json}";
            http_respond(client_fd, 200, "OK", "application/json",
                         json, strlen(json));
        } else {
            http_respond(client_fd, 404, "Not Found", "text/plain",
                         "Unknown", 7);
        }

        close(client_fd);
    }
    return NULL;
}

static TestServer *srv_start(vv_Net *net) {
    TestServer *srv = calloc(1, sizeof(TestServer));
    if (!srv) return NULL;
    srv->net = net;
    srv->listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (srv->listen_fd < 0) { free(srv); return NULL; }

    int opt = 1;
    setsockopt(srv->listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = htonl(INADDR_LOOPBACK),
        .sin_port = 0, // ephemeral port
    };
    if (bind(srv->listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(srv->listen_fd); free(srv); return NULL;
    }
    if (listen(srv->listen_fd, 8) < 0) {
        close(srv->listen_fd); free(srv); return NULL;
    }
    // Get the assigned port.
    socklen_t alen = sizeof(addr);
    getsockname(srv->listen_fd, (struct sockaddr *)&addr, &alen);
    srv->port = ntohs(addr.sin_port);

    if (pthread_create(&srv->thread, NULL, server_thread, srv) != 0) {
        close(srv->listen_fd); free(srv); return NULL;
    }
    return srv;
}

static void srv_stop(TestServer *srv) {
    if (!srv) return;
    srv->stop = 1;
    // Wake up accept() by connecting to ourselves.
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd >= 0) {
        struct sockaddr_in addr = {
            .sin_family = AF_INET,
            .sin_addr.s_addr = htonl(INADDR_LOOPBACK),
            .sin_port = htons((uint16_t)srv->port),
        };
        connect(fd, (struct sockaddr *)&addr, sizeof(addr));
        close(fd);
    }
    pthread_join(srv->thread, NULL);
    close(srv->listen_fd);
    free(srv);
}

static char *srv_url(TestServer *srv, const char *path) {
    char *url = malloc(256);
    snprintf(url, 256, "http://127.0.0.1:%d%s", srv->port, path);
    return url;
}

// ===========================================================================
//  Tests
// ===========================================================================

static void msleep(unsigned ms) {
    struct timespec ts = { .tv_sec = ms / 1000, .tv_nsec = (ms % 1000) * 1000000 };
    nanosleep(&ts, NULL);
}

// Wait for a request to complete (poll until done or timeout).
static bool wait_done(vv_NetRequest *r, int timeout_ms) {
    int spins = 0;
    while (!vv_net_response(r) && spins * 10 < timeout_ms) {
        msleep(10);
        spins++;
    }
    return vv_net_response(r) != NULL;
}

static void run_tests(void) {
    // Ignore SIGPIPE: writing to a closed client socket in the fixture server
    // should fail gracefully, not kill the process.
    struct sigaction sa;
    sa.sa_handler = SIG_IGN;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGPIPE, &sa, NULL);

    vv_NetConfig cfg = vv_net_config_default();
    vv_Net *net = vv_net_create(&cfg);
    CHECK(net != NULL);

    TestServer *srv = srv_start(net);
    CHECK(srv != NULL);

    // --- GET: basic text response ---
    {
        char *url = srv_url(srv, "/hello");
        vv_NetRequest *r = vv_net_start(net, &(vv_NetSpec){
            .method = "GET", .url = url, .complete_msg = VV_MSG_NONE,
        });
        CHECK(r != NULL);
        CHECK(wait_done(r, 5000));
        const vv_NetResponse *resp = vv_net_response(r);
        CHECK(resp != NULL);
        CHECK(resp->error == VV_NET_OK);
        CHECK(resp->status == 200);
        CHECK(resp->body_len == 13);
        CHECK(memcmp(resp->body, "Hello, Verve!", 13) == 0);
        CHECK(vv_net_succeeded(r));
        free(url);
        vv_net_request_release(r);
    }

    // --- POST: echo body ---
    {
        char *url = srv_url(srv, "/echo");
        const char *payload = "POST body data";
        vv_NetRequest *r = vv_net_start(net, &(vv_NetSpec){
            .method = "POST", .url = url,
            .body = payload, .body_len = strlen(payload),
            .complete_msg = VV_MSG_NONE,
        });
        CHECK(r != NULL);
        CHECK(wait_done(r, 5000));
        const vv_NetResponse *resp = vv_net_response(r);
        CHECK(resp != NULL);
        CHECK(resp->error == VV_NET_OK);
        CHECK(resp->status == 200);
        CHECK(resp->body_len == strlen(payload));
        CHECK(memcmp(resp->body, payload, strlen(payload)) == 0);
        free(url);
        vv_net_request_release(r);
    }

    // --- Headers: custom request header echoed back as JSON ---
    {
        char *url = srv_url(srv, "/headers");
        vv_NetHeader hdrs[] = { { "X-Custom", "verve-test" } };
        vv_NetRequest *r = vv_net_start(net, &(vv_NetSpec){
            .method = "GET", .url = url,
            .headers = hdrs, .header_count = 1,
            .complete_msg = VV_MSG_NONE,
        });
        CHECK(r != NULL);
        CHECK(wait_done(r, 5000));
        const vv_NetResponse *resp = vv_net_response(r);
        CHECK(resp != NULL);
        CHECK(resp->error == VV_NET_OK);
        CHECK(resp->status == 200);
        // Check that the response body contains our header value.
        CHECK(memmem(resp->body, resp->body_len, "verve-test", 10) != NULL);
        // Also check the response headers.
        const char *ct = vv_net_header(r, "Content-Type");
        CHECK(ct != NULL);
        CHECK(strstr(ct, "application/json") != NULL);
        free(url);
        vv_net_request_release(r);
    }

    // --- Binary body: NUL bytes preserved ---
    {
        char *url = srv_url(srv, "/binary");
        vv_NetRequest *r = vv_net_start(net, &(vv_NetSpec){
            .method = "GET", .url = url, .complete_msg = VV_MSG_NONE,
        });
        CHECK(r != NULL);
        CHECK(wait_done(r, 5000));
        const vv_NetResponse *resp = vv_net_response(r);
        CHECK(resp != NULL);
        CHECK(resp->error == VV_NET_OK);
        CHECK(resp->status == 200);
        CHECK(resp->body_len == 256);
        // Verify the bytes are 0,1,2,...,255
        bool ok = true;
        for (size_t i = 0; i < 256; i++) {
            if (resp->body[i] != (uint8_t)i) { ok = false; break; }
        }
        CHECK(ok);
        free(url);
        vv_net_request_release(r);
    }

    // --- 4xx response ---
    {
        char *url = srv_url(srv, "/notfound");
        vv_NetRequest *r = vv_net_start(net, &(vv_NetSpec){
            .method = "GET", .url = url, .complete_msg = VV_MSG_NONE,
        });
        CHECK(r != NULL);
        CHECK(wait_done(r, 5000));
        const vv_NetResponse *resp = vv_net_response(r);
        CHECK(resp != NULL);
        CHECK(resp->error == VV_NET_OK);  // transport succeeded
        CHECK(resp->status == 404);
        CHECK(!vv_net_succeeded(r));      // 4xx is not a success
        CHECK(vv_net_error(r) == VV_NET_OK); // no transport error
        free(url);
        vv_net_request_release(r);
    }

    // --- 5xx response ---
    {
        char *url = srv_url(srv, "/error");
        vv_NetRequest *r = vv_net_start(net, &(vv_NetSpec){
            .method = "GET", .url = url, .complete_msg = VV_MSG_NONE,
        });
        CHECK(r != NULL);
        CHECK(wait_done(r, 5000));
        const vv_NetResponse *resp = vv_net_response(r);
        CHECK(resp != NULL);
        CHECK(resp->error == VV_NET_OK);
        CHECK(resp->status == 500);
        CHECK(!vv_net_succeeded(r));
        free(url);
        vv_net_request_release(r);
    }

    // --- Timeout: /slow takes 500ms, set 100ms timeout ---
    {
        char *url = srv_url(srv, "/slow");
        vv_NetRequest *r = vv_net_start(net, &(vv_NetSpec){
            .method = "GET", .url = url,
            .timeout_ms = 100, .connect_timeout_ms = 100,
            .complete_msg = VV_MSG_NONE,
        });
        CHECK(r != NULL);
        CHECK(wait_done(r, 5000));
        const vv_NetResponse *resp = vv_net_response(r);
        CHECK(resp != NULL);
        CHECK(resp->error == VV_NET_ERR_TIMEOUT);
        CHECK(resp->status == 0);
        free(url);
        vv_net_request_release(r);
    }

    // --- Response size limit: /big returns 2MiB, limit to 1KiB ---
    {
        char *url = srv_url(srv, "/big");
        vv_NetRequest *r = vv_net_start(net, &(vv_NetSpec){
            .method = "GET", .url = url,
            .max_response_bytes = 1024,
            .complete_msg = VV_MSG_NONE,
        });
        CHECK(r != NULL);
        CHECK(wait_done(r, 5000));
        const vv_NetResponse *resp = vv_net_response(r);
        CHECK(resp != NULL);
        CHECK(resp->error == VV_NET_ERR_RESPONSE_TOO_LARGE);
        free(url);
        vv_net_request_release(r);
    }

    // --- Cancellation: start /slow, cancel before it finishes ---
    {
        char *url = srv_url(srv, "/slow");
        vv_NetRequest *r = vv_net_start(net, &(vv_NetSpec){
            .method = "GET", .url = url,
            .timeout_ms = 5000, .connect_timeout_ms = 5000,
            .complete_msg = VV_MSG_NONE,
        });
        CHECK(r != NULL);
        msleep(50); // let it start
        vv_net_request_release(r); // non-blocking cancel
        // Wait a bit for the worker to finish.
        msleep(600);
        // The request should have been freed; just verify no crash.
        free(url);
    }

    // --- JSON parsing: valid JSON ---
    {
        char *url = srv_url(srv, "/json");
        vv_NetRequest *r = vv_net_start(net, &(vv_NetSpec){
            .method = "GET", .url = url,
            .parse_json = true, .complete_msg = VV_MSG_NONE,
        });
        CHECK(r != NULL);
        CHECK(wait_done(r, 5000));
        const vv_NetResponse *resp = vv_net_response(r);
        CHECK(resp != NULL);
        CHECK(resp->error == VV_NET_OK);
        CHECK(resp->json != NULL);
        const vv_JsonValue *root = vv_json_root(resp->json);
        CHECK(root != NULL);
        CHECK(vv_json_kind(root) == VV_JSON_OBJECT);
        const vv_JsonValue *name = vv_json_object_get(root, "name");
        CHECK(name != NULL);
        CHECK(vv_json_kind(name) == VV_JSON_STRING);
        const char *s = NULL; size_t slen = 0;
        CHECK(vv_json_string(name, &s, &slen));
        CHECK(s != NULL);
        CHECK(strcmp(s, "verve") == 0);
        const vv_JsonValue *ver = vv_json_object_get(root, "version");
        CHECK(ver != NULL);
        double d = 0;
        CHECK(vv_json_number(ver, &d));
        CHECK((int)d == 42);
        const vv_JsonValue *active = vv_json_object_get(root, "active");
        CHECK(active != NULL);
        bool b = false;
        CHECK(vv_json_bool(active, &b));
        CHECK(b == true);
        const vv_JsonValue *items = vv_json_object_get(root, "items");
        CHECK(items != NULL);
        CHECK(vv_json_kind(items) == VV_JSON_ARRAY);
        CHECK(vv_json_array_count(items) == 3);
        const vv_JsonValue *first = vv_json_array_at(items, 0);
        CHECK(first != NULL);
        CHECK(vv_json_number(first, &d));
        CHECK((int)d == 1);
        free(url);
        vv_net_request_release(r);
    }

    // --- JSON parsing: invalid JSON ---
    {
        char *url = srv_url(srv, "/badjson");
        vv_NetRequest *r = vv_net_start(net, &(vv_NetSpec){
            .method = "GET", .url = url,
            .parse_json = true, .complete_msg = VV_MSG_NONE,
        });
        CHECK(r != NULL);
        CHECK(wait_done(r, 5000));
        const vv_NetResponse *resp = vv_net_response(r);
        CHECK(resp != NULL);
        CHECK(resp->error == VV_NET_ERR_JSON);
        CHECK(resp->json == NULL);
        CHECK(resp->error_message != NULL);
        free(url);
        vv_net_request_release(r);
    }

    // --- vv_net_poll: retrieve completion manually ---
    {
        char *url = srv_url(srv, "/hello");
        vv_NetRequest *r = vv_net_start(net, &(vv_NetSpec){
            .method = "GET", .url = url, .complete_msg = VV_MSG_NONE,
        });
        CHECK(r != NULL);
        CHECK(wait_done(r, 5000));
        // Drain any prior completions first.
        vv_NetCompletion comp;
        while (vv_net_poll(net, &comp)) {
            if (comp.request == r) break;
            // Release any other completions we pick up.
            vv_net_request_release(comp.request);
        }
        const vv_NetResponse *resp = vv_net_response(r);
        CHECK(resp != NULL);
        CHECK(resp->status == 200);
        free(url);
        vv_net_request_release(r);
        // Drain remaining.
        while (vv_net_poll(net, &comp))
            vv_net_request_release(comp.request);
    }

    // --- vv_net_dispatch: emit message into context ---
    {
        vv_Ctx ctx;
        vv_init(&ctx);
        vv_set_window(&ctx, 320, 240, 1.0f);

        enum { MSG_HELLO_DONE = 1 };  // an ordinary app message id
        char *url = srv_url(srv, "/hello");
        vv_NetRequest *r = vv_net_start(net, &(vv_NetSpec){
            .method = "GET", .url = url,
            .complete_msg = MSG_HELLO_DONE, .tag = (void *)0x1234,
        });
        CHECK(r != NULL);
        CHECK(wait_done(r, 5000));

        // Dispatch should emit the message.
        size_t emitted = vv_net_dispatch(net, &ctx);
        CHECK(emitted >= 1);

        // The message should be in the event queue.
        vv_Event ev;
        bool found = false;
        while (vv_poll_event(&ctx, &ev)) {
            if (ev.msg == MSG_HELLO_DONE) {
                found = true;
                CHECK(ev.data.as_ptr == r);
            }
        }
        CHECK(found);

        // Now release the request (it was already dispatched, so it's been
        // consumed by vv_net_dispatch and freed — but we still hold the pointer).
        // Actually, vv_net_dispatch keeps the request alive until release.
        // The request was emitted with vv_pp(request), so the user gets the
        // pointer and must release it.
        vv_net_request_release(r);
        free(url);
        vv_shutdown(&ctx);
    }

    // --- Non-blocking release of a running request ---
    {
        char *url = srv_url(srv, "/slow");
        vv_NetRequest *r = vv_net_start(net, &(vv_NetSpec){
            .method = "GET", .url = url,
            .timeout_ms = 5000, .connect_timeout_ms = 5000,
            .complete_msg = VV_MSG_NONE,
        });
        CHECK(r != NULL);
        msleep(50);
        // Release while running — must be non-blocking.
        struct timespec t0, t1;
        clock_gettime(CLOCK_MONOTONIC, &t0);
        vv_net_request_release(r);
        clock_gettime(CLOCK_MONOTONIC, &t1);
        double elapsed = (double)(t1.tv_sec - t0.tv_sec) +
                         (double)(t1.tv_nsec - t0.tv_nsec) / 1e9;
        CHECK(elapsed < 1.0); // should return almost immediately
        // Wait for cleanup.
        msleep(600);
        free(url);
    }

    // --- NULL safety ---
    {
        CHECK(vv_net_response(NULL) == NULL);
        CHECK(!vv_net_succeeded(NULL));
        CHECK(vv_net_error(NULL) == VV_NET_OK);
        CHECK(vv_net_progress(NULL) == 0.0f);
        CHECK(vv_net_header(NULL, "x") == NULL);
        vv_net_request_release(NULL);
        CHECK(!vv_net_poll(NULL, NULL));
        CHECK(vv_net_dispatch(NULL, NULL) == 0);
        CHECK(vv_json_root(NULL) == NULL);
        CHECK(vv_json_kind(NULL) == VV_JSON_NULL);
        CHECK(vv_json_object_get(NULL, "x") == NULL);
        CHECK(vv_json_array_count(NULL) == 0);
        CHECK(vv_json_array_at(NULL, 0) == NULL);
        const char *s = NULL; size_t l = 0;
        CHECK(!vv_json_string(NULL, &s, &l));
        double d = 0;
        CHECK(!vv_json_number(NULL, &d));
        bool b = false;
        CHECK(!vv_json_bool(NULL, &b));
    }

    srv_stop(srv);
    vv_net_destroy(net);
}

TEST_MAIN()

#include "../http/http_server.h"
#include <WinSock2.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <minwindef.h>

// CORS preflight: the OPTIONS before a cross origin fetch
// so only headers no body
static void handle_options(HttpServerRequest *req, HttpServerResponse *res) {
  printf("[%s %s] preflight\n", req->method, req->path);
  http_res_text(res, 200, "text/plain", NULL, 0);
}

static void handle_ping(HttpServerRequest *req, HttpServerResponse *res) {
  printf("[%s %s] %d headers\n", req->method, req->path, req->header_count);
  const char *ua = http_req_header(req, "User-Agent");
  if (ua) {
    printf(" user-agent: %s\n", ua);
  }
  http_res_text(res, 200, "application/json", "{\"pong\":true}", -1);
}

// prefix route: matches /echo/anything, and shows off wuery + body access
static void handle_echo(HttpServerRequest *req, HttpServerResponse *res) {
  printf("[%s %s] body %d bytes\n", req->method, req->path, req->body_len);

  int cap = req->body_len + HTTP_PATH_LEN + HTTP_QUERY_LEN + 64;
  char *out = (char *)malloc(cap);
  if (!out) {
    http_res_text(res, 500, "text/plain", "out of memory", -1);
    return;
  }
  int n =
      snprintf(out, cap, "method=%s\npath=%s\nquery=%s\nbody=%.*s\n",
               req->method, req->path, req->query, req->body_len, req->body);
  if (n < 0) {
    n = 0;
  }
  if (n >= cap) {
    n = cap - 1;
  }
  http_res_own(res, 200, "text/plain", out, n);
}

int main() {
  WSADATA wsaData;
  if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
    return 1;
  }

  HttpServer srv;
  http_server_route(&srv, "OPTIONS", "*", handle_options);
  http_server_route(&srv, "GET", "/ping", handle_ping);
  http_server_route(&srv, NULL, "/echo/", handle_echo);

  if (!http_server_start(&srv, "61616")) {
    WSACleanup();
    return 1;
  }
  printf("listening on http://127.0.0.1:61616\n");
  http_server_run(&srv);

  http_server_stop(&srv);
  WSACleanup();
  return 0;
}

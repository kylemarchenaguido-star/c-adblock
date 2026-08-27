#pragma once
#include "../net/socket.h"
#include "http_common.h"

#define HTTP_METHOD_LEN 16
#define HTTP_PATH_LEN 512
#define HTTP_QUERY_LEN 512
#define HTTP_MAX_ROUTES 16

struct HttpServerRequest {
  char method[HTTP_METHOD_LEN] = {0};
  char path[HTTP_PATH_LEN] = {0}; // no query string, no '?'
  char query[HTTP_QUERY_LEN] = {0}; // raw query string, without the '?'
  HttpHeader headers[HTTP_MAX_HEADERS];
  int header_count = 0;
  char *raw = NULL; // onws the whole request, headers + body
  int raw_len = 0;
  const char *body = NULL; // points inside raw
  int body_len = 0;
};

struct HttpServerResponse {
  int status = 200;
  const char *content_type = "text/plain";
  const char *body = NULL;
  int body_len = 0;
  int body_owned = 0; // if 1m the server free's body after sending it 
};

typedef void (*HttpHandler)(HttpServerRequest *req, HttpServerResponse *res);

struct HttpRoute {
  const char *method; // "GET"/"POST"/..., or NULL to match any method
  const char *path; // exact "/ping", prefix "/live/*", or "*" for everything
  HttpHandler handler;
};

struct HttpServer {
  TcpListener listener;
  HttpRoute routes[HTTP_MAX_ROUTES];
  int route_count = 0;
  volatile int running = 0; 
};

int http_server_route(HttpServer *srv, const char *method, const char *path, HttpHandler handler);
int http_server_start(HttpServer *srv, const char *port);
void http_server_run(HttpServer *srv);
void http_server_stop(HttpServer *srv);

const char *http_req_header(HttpServerRequest *req, const char *name);
void http_res_text(HttpServerResponse *res, int status, const char *content_type, const char *body, int body_len);
void http_res_own(HttpServerResponse *res, int status, const char *content_type, char *body, int body_len);

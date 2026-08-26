#pragma once
#include "../net/tls_channel.h"

#define HTTP_MAX_HEADERS 32
#define HTTP_NAME_LEN 64
#define HTTP_VALUE_LEN 1024

struct HttpHeader {
  char name[HTTP_NAME_LEN];
  char value[HTTP_VALUE_LEN];
};

struct HttpRequest {
  const char *method = "GET";
  const char *host = NULL;
  const char *port = "443";
  const char *path = "/";
  HttpHeader headers[HTTP_MAX_HEADERS];
  int header_count = 0;
  const char *body = NULL;
  int body_len = 0;
};

struct HttpResponse {
  int status = 0;
  HttpHeader headers[HTTP_MAX_HEADERS];
  int header_count = 0;
  char *raw = NULL; // owns the whole response, headers + body
  char *body = NULL; // points inside raw
  int body_len = 0;
};

int http_add_header(HttpRequest *req, const char *name, const char *value);
int http_request(CredHandle *cred, HttpRequest *req, HttpResponse *res);
const char* http_get_header(HttpRequest *res, const char *name);
void http_response_free(HttpResponse *res);

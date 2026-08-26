#pragma once
#include "../net/socket.cpp"
#include "http_common.h"

#define HTTP_METHOD_LEN 16
#define HTTP_PATH_LEN 512
#define HTTP_QUERY_LEN 512
#define HTTP_MAX_ROUTES 16

struct HtppServerRequest {
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

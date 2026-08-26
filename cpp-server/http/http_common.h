#pragma once

// shared by the client and the server, it has no dependency on winsock or schannel

#define HTTP_MAX_HEADERS 32
#define HTTP_NAME_LEN 64
#define HTTP_VALUE_LEN 1024

struct HttpHeader {
  char name[HTTP_NAME_LEN];
  char value[HTTP_VALUE_LEN];
};

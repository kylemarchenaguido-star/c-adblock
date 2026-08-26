#include "http_client.h"
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string.h>

int http_add_header(HttpRequest *req, const char *name, const char *value){
  if (req->header_count >= HTTP_MAX_HEADERS){ return 0; };
  HttpHeader *h = &req->headers[req->header_count];
  snprintf(h->name, HTTP_NAME_LEN, "%s", name);
  snprintf(h->value, HTTP_VALUE_LEN, "%s", value);
  req->header_count++;
  return 1;
}

const char* http_get_header(HttpResponse *res, const char *name){
  for (int i = 0; i < res->header_count; ++i){
    if (_stricmp(res->headers[i].name, name) == 0){ return res->headers[i].value; }
  }
  return NULL;
}

void http_response_free(HttpResponse *res){
  free(res->raw);
  res->raw = NULL;
  res->body =  NULL;
  res->body_len = 0;
}

// appends onto head, returns new n, or -1 on error
static int head_append(char *head, int cap, int n, const char *fmt, ...){
  if (n < 0 || n >= cap){ return -1; }
  va_list args;
  va_start(args, fmt);
  int added = vsnprintf(head + n, cap - n, fmt, args);
  va_end(args);
  if (added < 0 || n + added >= cap){ return -1; }
  return n + added;
}

static int find_header_end(const char *buf, int len){
  for (int i = 0; i + 3 < len; ++i){
    if (buf[i] == '\r' && buf[i + 1] == '\n' && buf[i + 2] == '\r' && buf[i + 3] == '\n'){
      return i + 4;
    }
  }
  return -1;
}

// grows *buf if full, then reads more bytes in, return 0 on EOF/error
static int recv_more(TlsConn *conn, char **buf, int *len, int *cap){
  if (*len == *cap){
    int new_cap = *cap * 2;
    char *grown = (char *)realloc(*buf, new_cap); 
    if (!grown) { return 0; }
    *buf = grown;
    *cap = new_cap;
  }
  int n = tls_recv(conn, *buf + *len, *cap - *len);
  if (n <= 0){ return 0; }
  *len += n;
  return 1;
}

// decodes "Transfer-Encoding: chunked" body in place, starting at *buf+pos, overwriting
// the chunk-size/CRLF framing so the decoded bytes end up contiguous at *buf+pos.
// returns decoded body length, or -1 if the connection closed mid-chunk.
static int dechunk(TlsConn *conn, char **buf, int *len, int *cap, int pos){
  int write_pos = pos;
  int read_pos = pos;

  for (;;){
    int line_end = -1;
    while (line_end == -1) {
      for (int i = read_pos; i + 1 < *len; ++i){
        if ((*buf)[i] == '\r' && (*buf)[i + 1] == '\n'){ line_end = i; break; }
      }
      if (line_end == -1 && !recv_more(conn, buf, len, cap)){ return -1; }
    }

    int chunk_size = (int)strtol(*buf + read_pos, NULL, 16);
    read_pos = line_end + 2;

    if (chunk_size == 0){ break; }// final chunk
    
    while (*len - read_pos < chunk_size + 2){
      if (!recv_more(conn, buf, len, cap)){ return -1; }
    }

    memmove(*buf + write_pos, *buf + read_pos, chunk_size);
    write_pos += chunk_size;
    read_pos += chunk_size + 2; // chunck data + trailing CRLF
  }
  return write_pos - pos;
}

static void parse_status_and_headers(const char *buf, int header_end, HttpResponse *res){
  const char *sp1 = (const char *)memchr(buf, ' ', header_end);
  res->status = sp1 ? atoi(sp1 + 1) : 0;

  const char *line = (const char*)memchr(buf, '\n', header_end);
  if (!line){ return; }
  line++;

  const char *buf_end = buf + header_end;
  res->header_count = 0;
  while (line < buf_end && res->header_count < HTTP_MAX_HEADERS){
    const char *line_end = (const char*)memchr(line, '\n', buf_end - line);
    if (!line_end){ break; }
    int line_len = (int)(line_end - line);
    if (line_len <= 1){ break; } // bare "\r\n", end of headers
  
    const char *colon = (const char*)memchr(line, ':', line_len);
    if (colon){
      int name_len = (int)(colon - line);
      const char *val = colon + 1;
      int val_len = (int)(line_end - val);
      while (val_len > 0 && *val == ' '){ val++; val_len--; }
      while (val_len > 0 && (val[val_len - 1] == '\r' || val[val_len - 1] == '\n')){ val_len--; }

      if (name_len >= HTTP_NAME_LEN){ name_len = HTTP_NAME_LEN - 1; }
      if (val_len >= HTTP_VALUE_LEN){ val_len = HTTP_VALUE_LEN - 1; }

      HttpHeader *h = &res->headers[res->header_count++];
      memcpy(h->name, line, name_len);
      h->name[name_len] = '\0';
      memcpy(h->value, val, val_len);
      h->value[val_len] = '\0';
    }
    line = line_end + 1;
  }
}

int http_request(CredHandle *cred, HttpRequest *req, HttpResponse *res){
  TlsConn conn;
  if (!tls_connect(&conn, cred, req->host, req->port)){
    printf("http_request failed: tls_connect to %s failed\n", req->host);
    return 0;
  }

  char head[8192];
  int n = 0;
  n = head_append(head, sizeof(head), n, "%s %s HTTP/1.1\r\n", req->method, req->path); 
  n = head_append(head, sizeof(head), n, "Host: %s\r\n",req->host);
  n = head_append(head, sizeof(head), n, "Connection: close\r\n");

  for (int i = 0; i < req->header_count && n >= 0; ++i){
    n = head_append(head, sizeof(head), n, "%s: %s\r\n", req->headers[i].name, req->headers[i].value);
  }
  if (n >= 0 && req->body_len > 0){
    n = head_append(head, sizeof(head), n, "Content-Length: %d\r\n", req->body_len);
  }
  if (n >= 0) {
    n = head_append(head, sizeof(head), n, "\r\n");
  }
  if (n < 0) {
    printf("http_request: request head too large for buffer\n");
    tls_close(&conn);
    return 0;
  }

  if (!tls_send(&conn, head, n)){
    tls_close(&conn);
    return 0;
  }

  if (req->body_len > 0 && !tls_send(&conn, req->body, req->body_len)){
    tls_close(&conn);
    return 0;
  }

  int cap = 8912, len = 0;
  char *buf = (char*)malloc(cap);
  int header_end = -1;

  while (header_end == -1){
    if (!recv_more(&conn, &buf, &len, &cap)){
      printf("http_request: connection closed before headers finished\n");
      free(buf);
      tls_close(&conn);
      return  0;
    }
    header_end = find_header_end(buf, len);
  }

  parse_status_and_headers(buf, header_end, res);

  const char *te = http_get_header(res, "Transfer-Encoding");
  if (te && strstr(te, "chunked")){
    int body_len = dechunk(&conn, &buf, &len, &cap, header_end);
    if (body_len < 0){
      printf("http_request: connection closed mid-chunk\n");
      free(buf);
      tls_close(&conn);
      return 0;
    }
    res->raw = buf;
    res->body = buf + header_end;
    res->body_len = len - header_end;

    tls_close(&conn);
    return 1;
  }
}


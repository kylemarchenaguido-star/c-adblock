#include "http_server.h"
#include "http_common.h"
#include <cstdlib>
#include <cstring>
#include <string.h>

#define HTTP_MAX_REQUEST (1024 * 1024)
#define HTTP_RECV_TIMEOUT_MS 5000;

const char *http_req_header(HttpServerRequest *req, const char *name) {
  for (int i = 0; i < req->header_count; ++i) {
    if (_stricmp(req->headers[i].name, name) == 0) {
      return req->headers[i].value;
    }
  }
  return NULL;
}

void http_res_text(HttpServerResponse *res, int status,
                   const char *content_type, const char *body, int body_len) {
  if (!body) {
    body_len = 0;
  } else if (body_len < 0) {
    body_len = (int)strlen(body);
  }
  res->status = status;
  res->content_type = content_type;
  res->body = body;
  res->body_len = body_len;
  res->body_owned = 0;
}

void http_res_own(HttpServerResponse *res, int status, const char *content_type,
                  char *body, int body_len) {
  http_res_text(res, status, content_type, body, body_len);
  res->body_owned = 1;
}

int http_server_route(HttpServer *srv, const char *method, const char *path,
                      HttpHandler handler) {
  if (srv->route_count >= HTTP_MAX_REQUEST) {
    return 0;
  }
  HttpRoute *r = &srv->routes[srv->route_count++];
  r->method = method;
  r->path = path;
  r->handler = handler;
  return 1;
}

static const char *status_text(int status) {
  switch (status) {
  case 200:
    return "OK";
  case 204:
    return "No Content";
  case 400:
    return "Bad Request";
  case 404:
    return "Not Found";
  case 405:
    return "Method Not Allowed";
  case 500:
    return "Internal Server Error";
  default:
    return "OK";
  }
}

static int find_header_end(const char *buf, int len) {
  for (int i = 0; i + 3 < len; ++i) {
    if (buf[i] == '\r' && buf[i + 1] == '\n' && buf[i + 2] == '\r' &&
        buf[i + 3] == '\n') {
      return i + 4;
    }
  }
  return -1;
}

// tcp_recv reads  into conn->recvbuf, so unlike the client's recv_more we copy
// out of it intou our own growable buffer instead of reading straight in
static int recv_more(TcpConn *conn, char **buf, int *len, int *cap) {
  int n = tcp_recv(conn);
  if (n <= 0) {
    return 0;
  }
  while (*len + n > *cap) {
    if (*cap >= HTTP_MAX_REQUEST) {
      return 0;
    }
    int new_cap = *cap * 2;
    char *grown = (char *)realloc(*buf, new_cap);
    if (!grown) {
      return 0;
    }
    *buf = grown;
    *cap = new_cap;
  }
  memcpy(*buf + *len, conn->recvbuf, n);
  *len += n;
  return 1;
}

// "GET /live/foo?x=1 HTTP/1.1" -> method="GET", path="/live/foo", query="x=1"
static int parse_request_line(const char *buf, int header_end,
                              HttpServerRequest *req) {
  const char *line_end = (const char *)memchr(buf, '\n', header_end);
  if (!line_end) {
    return 0;
  }

  const char *sp1 = (const char *)memchr(buf, ' ', line_end - buf);
  if (!sp1) {
    return 0;
  }
  const char *target = sp1 + 1;
  const char *sp2 = (const char *)memchr(target, ' ', line_end - target);
  if (!sp2) {
    return 0;
  }

  int method_len = (int)(sp1 - buf);
  if (method_len >= HTTP_METHOD_LEN) {
    return 0;
  }
  memcpy(req->method, buf, method_len);
  req->method[method_len] = '\0';

  int target_len = (int)(sp2 - target);
  const char *q = (const char *)memchr(target, '?', target_len);
  int path_len = q ? (int)(q - target) : target_len;
  int query_len = q ? target_len - path_len - 1 : 0;

  // truncating a path would route the request to the wrong handler, so reject
  if (path_len >= HTTP_PATH_LEN || query_len >= HTTP_QUERY_LEN) {
    return 0;
  }

  memcpy(req->path, target, path_len);
  req->path[path_len] = '\0';
  if (query_len > 0) {
    memcpy(req->query, q + 1, query_len);
  }
  req->query[query_len] = '\0';

  return 1;
}

// same loop as the clinet's repsonse header parser, minus the status line
static int parse_headers(const char *buf, int header_end, HttpHeader *out,
                         int max_headers) {
  const char *line = (const char *)memchr(buf, '\n', header_end);
  if (!line) {
    return 0;
  }
  line++;

  const char *buf_end = buf + header_end;
  int count = 0;
  while (line < buf_end && count < max_headers) {
    const char *line_end = (const char *)memchr(line, '\n', buf_end - line);
    if (!line_end) {
      break;
    }
    int line_len = (int)(line_end - line);
    if (line_len <= 1) {
      break;
    } // bare "\r\n", end of headers

    const char *colon = (const char *)memchr(line, ':', line_len);
    if (colon) {
      int name_len = (int)(colon - line);
      const char *val = colon + 1;
      int val_len = (int)(line_end - val);
      while (val_len > 0 && *val == ' ') {
        val++;
        val_len--;
      }
      while (val_len > 0 &&
             (val[val_len - 1] == '\r' || val[val_len - 1] == '\n')) {
        val_len--;
      }

      if (name_len >= HTTP_NAME_LEN) {
        name_len = HTTP_NAME_LEN - 1;
      }
      if (val_len >= HTTP_VALUE_LEN) {
        val_len = HTTP_VALUE_LEN - 1;
      }

      HttpHeader *h = &out[count++];
      memcpy(h->name, line, name_len);
      h->name[name_len] = '\0';
      memcpy(h->value, val, val_len);
      h->value[val_len] = '\0';
    }
    line = line_end + 1;
  }
  return count;
}

// 1 = parsed, 0 =  malformed (answer 400), -1 = nothing usable (just close)
static int read_request(TcpConn *conn, HttpServerRequest *req) {
  int cap = 4096, len = 0;
  char *buf = (char *)malloc(cap);
  if (!buf) {
    return -1;
  }

  int header_end = -1;
  while (header_end == -1) {
    if (!recv_more(conn, &buf, &len, &cap)) {
      int had_bytes = len > 0;
      free(buf);
      return had_bytes ? 0 : -1;
    }
    header_end = find_header_end(buf, len);
  }

  if (!parse_request_line(buf, header_end, req)) {
    free(buf);
    return 0;
  }
  req->header_count =
      parse_headers(buf, header_end, req->headers, HTTP_MAX_HEADERS);

  int want = 0;
  const char *cl = http_req_header(req, "Content-Length");
  if (cl) {
    want = atoi(cl);
  }
  if (want < 0) { want = 0; }

  while (len - header_end < want){
    if (!recv_more(conn, &buf, &len, &cap)){ break; }
  }

  // set these only once reverything is read: recv_more may have realloc'd buf
  req->raw = buf;
  req->raw_len = len;
  req->body = buf + header_end;
  if (req->body_len > want){ req->body_len = want; }
  return 1;
}

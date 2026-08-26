#include "../http/http_client.h"
#include <WinSock2.h>
#include <cstdio>
#include <minwindef.h>
#include <sspi.h>

int main(){
  WSAData wsaData;
  if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0){ return 1; }

  CredHandle cred;
  if (!tls_init(&cred)){ WSACleanup(); return 1; }

  // GET 
  {
    HttpRequest req;
    req.method = "GET";
    req.host = "www.twitch.tv";
    req.path = "/k3soju";

    HttpResponse res;
    if (!http_request(&cred, &req, &res)){
      printf("GET request failed\n");
    } else {
      printf("=== GET %s%s ===\n", req.host, req.path);
      printf("status: %d\n", res.status);
      for (int i = 0; i < res.header_count; ++i){
        printf(" %s: %s\n", res.headers[i].name, res.headers[i].value);
      }
      printf("body_len: %d\n", res.body_len);
      int show = res.body_len < 300 ? res.body_len : 300;
      printf("body (first %d bytes):\n%.*s\n\n", show, show, res.body);
      http_response_free(&res);
    }
  }
  
  // POST 
  {
    const char *json =
    "{\"operationName\":\"PlaybackAccessToken\",\"query\":\"\"}";

    HttpRequest req;
    req.method = "POST";
    req.host = "gql.twitch.tv";
    req.path = "/gql";
    req.body = json;
    req.body_len = (int)strlen(json);
    http_add_header(&req, "Content-Type", "application/json");
    http_add_header(&req, "Client-ID",
    "kimne78kx3ncx6brgo4mv6wki5h1ko");

    HttpResponse res;
    if (!http_request(&cred, &req, &res)){
      printf("POST request failed\n");
    } else {
      printf("=== POST %s%s ===\n", req.host, req.path);
      printf("status: %d\n", res.status);
      for (int i = 0; i < res.header_count; ++i){
        printf(" %s: %s\n", res.headers[i].name, res.headers[i].value);
      }
      printf("body (%d bytes): %.*s\n", res.body_len, res.body_len, res.body);
      http_response_free(&res);
    }
  }

  tls_shutdown(&cred);
  WSACleanup();
  return 0;
}

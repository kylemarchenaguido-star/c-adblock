#include "/tls_channel.h"

int main() {
  WSADATA wsaData;
  if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0){ return 1; }

  CredHandle cred;
  if (!tls_init(&cred)){ WSACleanup(); return 1; }

  TlsConn conn;
  if (!tls_connect(&conn, &cred, "www.twitch.tv", "443")){
    printf("tls_connect failed\n");
    tls_shutdown(&cred);
    WSACleanup();
    return 1;
  }
  printf("Tls handshake + cert validation ok\n");

  const char *request = "GET / HTTP/1.1\r\nHost: www.twitch.tv\r\nConnection: close\r\n\r\n";
  tls_send(&conn, request, (int)strlen(request));

  char buf[4096];
  int n, total = 0;
  while ((n = tls_recv(&conn, buf, sizeof(buf) - 1)) > 0){
    buf[n] = '\0';
    total += n;
    if (total < 2000) printf("%s", buf); 
  }
  printf("\n received %d bytes total\n", total);

  tls_close(&conn);
  tls_shutdown(&cred);
  WSACleanup();
  return 0;
}

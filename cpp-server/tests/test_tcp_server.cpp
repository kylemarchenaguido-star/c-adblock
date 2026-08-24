#include "../net/socket.h"
#include <thread>

static void handle_client(TcpConn conn) {
  int n;
  while ((n = tcp_recv(&conn)) > 0) {
    printf("received %d bytes\n", n);
    tcp_send(&conn, conn.recvbuf, n);   // echo back
  }
  printf("client disconnected\n");
  tcp_close(&conn);
}

int main(){
  WSADATA wsaData;
  if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0){ return 1; }

  TcpListener listener;
  if (!tcp_listen(&listener, "61616")) {
    WSACleanup();
    return 1;
  }
  printf("listening on 127.0.0.1:61616\n");

  for (;;) {
    TcpConn conn;
    if (!tcp_accept(&listener, &conn)) continue;
    printf("client connected\n");
    std::thread(handle_client, conn).detach();
  }

  tcp_listener_close(&listener);
  WSACleanup();
  return 0;
}

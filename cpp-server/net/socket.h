#pragma once 
#include <WinSock2.h>
#include <minwindef.h>
#include <windows.h>
#include <WS2tcpip.h>
#include <stdio.h>
#include <stdlib.h>

#define DEFAULT_BUFLEN 512
#define DEFAULT_PORT "27015"


struct TcpConn {
  SOCKET handle = INVALID_SOCKET;
  char recvbuf[DEFAULT_BUFLEN];
  int recvbuflen = DEFAULT_BUFLEN;
};

int tcp_send(TcpConn *sock, const char *buf, int len);
int tcp_recv(TcpConn *sock);
void tcp_close(TcpConn *sock);


struct TcpSocket {
  TcpConn conn;
  struct addrinfo *result = NULL,
                  *ptr = NULL,
                  hints;
  const char *server_name;
};

int tcp_connect(TcpSocket *sock, const char *host, const char *port);

struct TcpListener {
  SOCKET ListenSocket = INVALID_SOCKET;

  struct addrinfo *result = NULL;
  struct addrinfo hints;
};

int tcp_listen(TcpListener *listener, const char *port);
int tcp_accept(TcpListener*sock, TcpConn *conn);
void tcp_listener_close(TcpListener *listener);



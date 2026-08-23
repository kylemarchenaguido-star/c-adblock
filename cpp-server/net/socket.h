#pragma once 
#include <WinSock2.h>
#include <windows.h>
#include <WS2tcpip.h>
#include <stdio.h>
#include <stdlib.h>

#pragma comment(lib, "Ws2_32.lib")
#pragma comment(lib, "Mswsock.lib")
#pragma comment(lib, "AdvApi32.lib")

#define DEFAULT_BUFLEN 512
#define DEFAULT_PORT "27015"


struct TcpSocket {
  SOCKET ConnectSocket = INVALID_SOCKET;
  struct addrinfo *result = NULL,
                  *ptr = NULL,
                  hints;
  const char *server_name;
  char recvbuf[DEFAULT_BUFLEN]; 
  int recvbuflen = DEFAULT_BUFLEN; 
};

int tcp_connect(TcpSocket *sock, const char *host, const char *port);
int tcp_send(TcpSocket *sock, const char *buf, int len);
int tcp_recv(TcpSocket *sock);
void tcp_close(TcpSocket *sock);




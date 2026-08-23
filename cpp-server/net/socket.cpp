#include "socket.h"
#include <WS2tcpip.h>
#include <WinSock2.h>
#include <cstdio>
#include <cstring>
#include <errhandlingapi.h>
#include <minwinbase.h>
#include <winbase.h>

int tcp_connect(TcpSocket *sock, const char *host, const char *port){
  ZeroMemory(&sock->hints, sizeof(sock->hints));
  sock->hints.ai_family = AF_UNSPEC;
  sock->hints.ai_socktype = SOCK_STREAM;
  sock->hints.ai_protocol = IPPROTO_IP;
  sock->server_name = host;


  int i = getaddrinfo(host, port, &sock->hints, &sock->result);
  if (i != 0){ printf("%d\n", i); return 0; }

  for (sock->ptr = sock->result; sock->ptr != NULL; sock->ptr = sock->ptr->ai_next){
    sock->ConnectSocket = socket(sock->ptr->ai_family, sock->ptr->ai_socktype, sock->ptr->ai_protocol);
    if (sock->ConnectSocket == INVALID_SOCKET){ printf("%ld\n", GetLastError());continue; }
    if (connect(sock->ConnectSocket, sock->ptr->ai_addr, (int)sock->ptr->ai_addrlen) == 0){ break; }

    closesocket(sock->ConnectSocket);
    sock->ConnectSocket = INVALID_SOCKET;

  }
  freeaddrinfo(sock->result);

  sock->result = NULL;
  sock->ptr = NULL;
  return sock->ConnectSocket != INVALID_SOCKET;
}

int tcp_send(TcpSocket *sock, const char *buf, int len){
  int total = 0;
  while (total < len){
    int n = send(sock->ConnectSocket, buf + total, len - total, 0);
    if (n == SOCKET_ERROR){ return 0;}
    total += n;
  }
  return 1;
}

int tcp_recv(TcpSocket *sock){
  return recv(sock->ConnectSocket, sock->recvbuf, sock->recvbuflen - 1, 0);
}

void tcp_close(TcpSocket *sock){
  if (sock->ConnectSocket != INVALID_SOCKET){
    closesocket(sock->ConnectSocket);
    sock->ConnectSocket = INVALID_SOCKET;
  }
}

int main(){
  WSADATA wsaData;
  if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0){ return 1;}
  
  TcpSocket sock;
  if (!tcp_connect(&sock, "example.com", "80")){
    printf("Connect failed\n");
    WSACleanup();
    return 1;
  }
  const char* request =
      "GET / HTTP/1.1\r\nHost: example.com\r\nConnection: close\r\n\r\n";
  tcp_send(&sock, request, (int)strlen(request));
  int n = 0;
  while ((n = tcp_recv(&sock)) > 0){
    sock.recvbuf[n] = '\0';
    printf("%s", sock.recvbuf);
  }
  tcp_close(&sock);
  WSACleanup();
  return 0;
}

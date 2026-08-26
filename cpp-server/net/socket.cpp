#include "socket.h"
#include <WS2tcpip.h>
#include <WinSock2.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <errhandlingapi.h>
#include <minwinbase.h>
#include <minwindef.h>
#include <winbase.h>
//#include <thread>

#pragma comment(lib, "Ws2_32.lib")
#pragma comment(lib, "Mswsock.lib")
#pragma comment(lib, "AdvApi32.lib")

int tcp_connect(TcpSocket *sock, const char *host, const char *port){
  ZeroMemory(&sock->hints, sizeof(sock->hints));
  sock->hints.ai_family = AF_UNSPEC;
  sock->hints.ai_socktype = SOCK_STREAM;
  sock->hints.ai_protocol = IPPROTO_IP;
  sock->server_name = host;


  int i = getaddrinfo(host, port, &sock->hints, &sock->result);
  if (i != 0){ printf("%d\n", i); return 0; }

  for (sock->ptr = sock->result; sock->ptr != NULL; sock->ptr = sock->ptr->ai_next){
    sock->conn.handle = socket(sock->ptr->ai_family, sock->ptr->ai_socktype, sock->ptr->ai_protocol);
    if (sock->conn.handle == INVALID_SOCKET){ 
      printf("%ld\n", GetLastError());
      continue;
    }
    if (connect(sock->conn.handle, sock->ptr->ai_addr, (int)sock->ptr->ai_addrlen) == 0){ break; }

    closesocket(sock->conn.handle);
    sock->conn.handle = INVALID_SOCKET;

  }
  freeaddrinfo(sock->result);

  sock->result = NULL;
  sock->ptr = NULL;
  return sock->conn.handle != INVALID_SOCKET;
}

int tcp_send(TcpConn *conn, const char *buf, int len){
  int total = 0;
  while (total < len){
    int n = send(conn->handle, buf + total, len - total, 0);
    if (n == SOCKET_ERROR){ return 0;}
    total += n;
  }
  return 1;
}

int tcp_recv(TcpConn *conn){
  return recv(conn->handle, conn->recvbuf, conn->recvbuflen - 1, 0);
}

void tcp_close(TcpConn *conn){
  if (conn->handle != INVALID_SOCKET){
    closesocket(conn->handle);
    conn->handle = INVALID_SOCKET;
  }
}

int tcp_listen(TcpListener *listener, const char *port){
  ZeroMemory(&listener->hints, sizeof(listener->hints));
  listener->hints.ai_family = AF_INET;
  listener->hints.ai_socktype = SOCK_STREAM;
  listener->hints.ai_protocol = IPPROTO_IP;

  int i = getaddrinfo("127.0.0.1", port, &listener->hints, &listener->result);
  if (i != 0){ printf("%d\n", i); return 0; }

  listener->ListenSocket = socket(listener->result->ai_family, listener->result->ai_socktype, listener->result->ai_protocol);
  if (listener->ListenSocket == INVALID_SOCKET){ 
    printf("socket failed: %d\n", WSAGetLastError());
    freeaddrinfo(listener->result);
    return 0;
  }
  i = bind(listener->ListenSocket, listener->result->ai_addr, (int)listener->result->ai_addrlen);
  if (i == SOCKET_ERROR){ 
    printf("bind failed: %d\n", WSAGetLastError());
    freeaddrinfo(listener->result);
    return 0;
  }

  freeaddrinfo(listener->result);
  listener->result = NULL;

  i = listen(listener->ListenSocket, SOMAXCONN);
  if (i == SOCKET_ERROR){ 
    printf("listen failed: %d\n", WSAGetLastError());
    closesocket(listener->ListenSocket);
    return 0;
  }
  return 1;
}

int tcp_accept(TcpListener *listener, TcpConn *conn){
  conn->handle = accept(listener->ListenSocket, NULL, NULL);
  if (conn->handle == INVALID_SOCKET){
    printf("accept failed: %d\n", WSAGetLastError());
    return 0;
  }
  return 1;
}

void tcp_listener_close(TcpListener *listener){
  if (listener->ListenSocket != INVALID_SOCKET){
    closesocket(listener->ListenSocket);
    listener->ListenSocket = INVALID_SOCKET;
  }
}





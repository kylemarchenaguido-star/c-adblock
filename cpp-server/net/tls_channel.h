#pragma once 
#include "socket.h"
#define SECURITY_WIN32
#include <security.h>
#include <schannel.h>
#include <wincrypt.h>
#include <sspi.h>

#pragma comment(lib, "secur32.lib")
#pragma comment(lib, "crypt32.lib")

#define TLS_BUF_SIZE 20000

struct TlsConn {
  TcpSocket tcp;
  CtxtHandle ctx;
  SecPkgContext_StreamSizes sizes;
  char raw_buf[TLS_BUF_SIZE]; // encrypted bytes read from the socket, not yet decrypted
  int raw_len = 0;
  char plain_buf[TLS_BUF_SIZE]; // decrypted bytes. waiting for the caller to read them
  int plain_len = 0, plain_pos = 0;
  char send_buf[TLS_BUF_SIZE]; // scratch space or EncryptMessage
};

int tls_init(CredHandle *cred);
int tls_connect(TlsConn *conn, CredHandle *cred, const char *host, const char *port);
int tls_send(TlsConn *conn, const char *buf, int len);
int tls_recv(TlsConn *conn, char *buf, int len);
void tls_close(TlsConn *conn);
void tls_shutdown(CredHandle *cred);

#include "tls_channel.h"
#include "socket.h"
#include <WinSock2.h>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <errhandlingapi.h>
#include <minschannel.h>
#include <minwindef.h>
#include <schannel.h>
#include <sspi.h>
#include <stringapiset.h>
#include <wincrypt.h>
#include <winerror.h>
#include <winnls.h>

// raw bytes, we do not use the tcp_send beacuse we need to read into raw_buf
// at different offset 
static int raw_send(SOCKET s, const char *buf, int len){
  int total = 0;
  while (total < len) {
    int n = send(s, buf + total, len - total, 0);
    if (n == SOCKET_ERROR){ return 0; }
    total += n;
  }
  return 1;
}

static int raw_recv(SOCKET s, char *buf, int len){
  return recv(s, buf, len, 0);
}

int tls_init(CredHandle *cred){
  SCHANNEL_CRED schannel_cred = {0};
  schannel_cred.dwVersion = SCHANNEL_CRED_VERSION;
  schannel_cred.grbitEnabledProtocols = SP_PROT_TLS1_2_CLIENT;
  schannel_cred.dwFlags = SCH_CRED_MANUAL_CRED_VALIDATION | SCH_CRED_NO_DEFAULT_CREDS;

  TimeStamp expiry;
  SECURITY_STATUS status = AcquireCredentialsHandleA(
      NULL, (SEC_CHAR*)UNISP_NAME_A, SECPKG_CRED_OUTBOUND,
      NULL, &schannel_cred, NULL, NULL, cred, &expiry);
  if (status != SEC_E_OK){
    printf("AcquireCredentialsHandle failed: 0x%lx\n", status);
    return 0;
  }
  return 1;
}

void tls_shutdown(CredHandle *cred){
  FreeCredentialHandle(cred);
}

// schannel does not validate the server certificate, we are doing it manually (lol)
static int tls_verify_cert(CtxtHandle *ctx, const char *host){
  PCCERT_CONTEXT server_cert = NULL;
  SECURITY_STATUS status = QueryContextAttributesA(ctx, SECPKG_ATTR_REMOTE_CERT_CONTEXT, &server_cert);
  if (status != SEC_E_OK || server_cert == NULL){
    printf("QueryContextAttributesA(REMOTE_CERT_CONTEXT) failed: 0x%lx\n", status);
    return 0;
  }
  CERT_CHAIN_PARA chain_para = {0};
  chain_para.cbSize = sizeof(chain_para);

  PCCERT_CHAIN_CONTEXT chain = NULL;
  BOOL ok = CertGetCertificateChain(NULL, server_cert, NULL, server_cert->hCertStore, &chain_para, 0, NULL, &chain);

  if (!ok) {
    printf("CertGetCertificateChain failed: %lu\n", GetLastError());
    CertFreeCertificateContext(server_cert);
    return 0;
  }

  wchar_t wide_host[256];
  MultiByteToWideChar(CP_UTF8, 0, host, -1, wide_host, 256);

  SSL_EXTRA_CERT_CHAIN_POLICY_PARA ssl_policy = {0};
  ssl_policy.cbSize = sizeof(ssl_policy);
  ssl_policy.dwAuthType = AUTHTYPE_CLIENT;
  ssl_policy.pwszServerName = wide_host;

  CERT_CHAIN_POLICY_PARA policy_para = {0};
  policy_para.cbSize = sizeof(policy_para);
  policy_para.pvExtraPolicyPara = &ssl_policy;

  CERT_CHAIN_POLICY_STATUS policy_status = {0};
  policy_status.cbSize = sizeof(policy_status);

  ok = CertVerifyCertificateChainPolicy(CERT_CHAIN_POLICY_SSL, chain, &policy_para, &policy_status);
  int trusted = ok && policy_status.dwError == 0;
  if (!trusted){
    printf("certificate chain policy error: 0x%lx\n", policy_status.dwError);
  }

  CertFreeCertificateChain(chain);
  CertFreeCertificateContext(server_cert);
  return trusted;
}


int tls_connect(TlsConn *conn, CredHandle *cred, const char *host, const char *port){
  if (!tcp_connect(&conn->tcp, host, port)){
    printf("tcp_connect failed\n");
    return 0;
  }
  
  DWORD ctx_req = ISC_REQ_SEQUENCE_DETECT | ISC_REQ_REPLAY_DETECT |
                  ISC_REQ_CONFIDENTIALITY | ISC_REQ_ALLOCATE_MEMORY | ISC_REQ_STREAM;
  DWORD ctx_attr;
  TimeStamp expiry;
  SECURITY_STATUS status;
  
  // no input yet, produces the ClientHello
  SecBuffer out_buf = { 0, SECBUFFER_TOKEN, NULL };
  SecBufferDesc out_desc = { SECBUFFER_VERSION, 1, &out_buf };

  status = InitializeSecurityContextA(cred, NULL, (SEC_CHAR*)host, ctx_req, 0, 0, 
                                      NULL, 0, &conn->ctx, &out_desc, &ctx_attr, &expiry);
  if (status != SEC_I_CONTINUE_NEEDED){
    printf("inital InitializeSecurityContextA failed: 0x%lx\n",status);
    tcp_close(&conn->tcp.conn);
    return 0;
  }
  if (out_buf.cbBuffer > 0){
    raw_send(conn->tcp.conn.handle, (char*)out_buf.pvBuffer, out_buf.cbBuffer);
    FreeContextBuffer(out_buf.pvBuffer);
  }

  int have = 0;
  for (;;){
    int n = raw_recv(conn->tcp.conn.handle, conn->raw_buf + have, sizeof(conn->raw_buf) - have);
    if (n <= 0){
      printf("server closed connection during handshake\n");
      tcp_close(&conn->tcp.conn);
      return 0;
    }
    int total = have + n;
    SecBuffer in_bufs[2];
    in_bufs[0] = { (unsigned long)total, SECBUFFER_TOKEN, conn->raw_buf };
    in_bufs[1] = { 0, SECBUFFER_EMPTY, NULL };
    SecBufferDesc in_desc = { SECBUFFER_VERSION, 2, in_bufs };

    out_buf = { 0, SECBUFFER_TOKEN, NULL };
    out_desc = { SECBUFFER_VERSION, 1, &out_buf };

    status = InitializeSecurityContextA(
       cred, &conn->ctx, (SEC_CHAR*)host, ctx_req, 0, 0, 
      &in_desc, 0, NULL, &out_desc, &ctx_attr, &expiry);

    if (status == SEC_E_INCOMPLETE_MESSAGE){
      have = total;
      continue;
    }

    if (out_buf.cbBuffer > 0 && out_buf.pvBuffer != NULL){
      raw_send(conn->tcp.conn.handle, (char*)out_buf.pvBuffer, out_buf.cbBuffer);
      FreeContextBuffer(out_buf.pvBuffer);
    }

    if (status == SEC_E_OK || status == SEC_I_CONTINUE_NEEDED) {
      have = 0;
      for (int i = 0; i < 2; ++i){
        if (in_bufs[i].BufferType == SECBUFFER_EXTRA){
          memmove(conn->raw_buf, conn->raw_buf + (total - in_bufs[i].cbBuffer), in_bufs[i].cbBuffer);
          have = in_bufs[i].cbBuffer;
        }
      }
      if (status == SEC_E_OK){
        // leftover are encrypted app data
        conn->raw_len = have;
        break;
      }
      continue;
    }
    printf("InitializeSecurityContextA failed: 0x%lx\n", status);
    tcp_close(&conn->tcp.conn);
    return 0;
  }

  if (!tls_verify_cert(&conn->ctx, host)){
    printf("certificate validation failed\n");
    DeleteSecurityContext(&conn->ctx);
    tcp_close(&conn->tcp.conn);
    return 0;
  }

  QueryContextAttributesA(&conn->ctx, SECPKG_ATTR_STREAM_SIZES, &conn->sizes);
  return 1;
}

int tls_send(TlsConn *conn, const char *buf, int len){
  int sent_total = 0;

  while (sent_total < len){
    int chunk = len - sent_total;
    if ((DWORD)chunk > conn->sizes.cbMaximumMessage){ chunk = conn->sizes.cbMaximumMessage; }

    char *msg = conn->send_buf;
    memcpy(msg + conn->sizes.cbHeader, buf + sent_total, chunk);

    SecBuffer buffers[4];
    buffers[0] = { conn->sizes.cbHeader, SECBUFFER_STREAM_HEADER, msg };
    buffers[1] = { (unsigned long)chunk, SECBUFFER_DATA, msg + conn->sizes.cbHeader };
    buffers[2] = { conn->sizes.cbTrailer, SECBUFFER_STREAM_TRAILER,  msg + conn->sizes.cbHeader + chunk };
    buffers[3] = { 0, SECBUFFER_EMPTY, NULL };
    SecBufferDesc desc = { SECBUFFER_VERSION, 4, buffers };

    SECURITY_STATUS status = EncryptMessage(&conn->ctx, 0, &desc, 0);
    if (status != SEC_E_OK){
      printf("EncryptMessage failed: 0x%lx\n", status);
      return 0;
    }
    
    int total_len = buffers[0].cbBuffer + buffers[1].cbBuffer + buffers[2].cbBuffer;
    if (!raw_send(conn->tcp.conn.handle, msg, total_len)){ return 0; }

    sent_total += chunk;
  }
  return 1;
}

int tls_recv(TlsConn *conn, char *buf, int len){
  if (conn->plain_pos < conn->plain_len) {
    int avail = conn->plain_len - conn->plain_pos;
    int n = (len < avail) ? len : avail;
    memcpy(buf, conn->plain_buf + conn->plain_pos, n);
    conn->plain_pos += n;
    if (conn->plain_pos == conn->plain_len){ conn->plain_len = 0; conn->plain_pos = 0; }
    return n;
  }

  for (;;){
    if (conn->raw_len == 0) {
      int n = raw_recv(conn->tcp.conn.handle, conn->raw_buf, sizeof(conn->raw_buf));
      if (n <= 0){ return 0; }
      conn->raw_len = n;
    }
    
    SecBuffer buffers[4];
    buffers[0] = { (unsigned long)conn->raw_len, SECBUFFER_DATA, conn->raw_buf };
    buffers[1] = { 0, SECBUFFER_EMPTY, NULL };
    buffers[2] = { 0, SECBUFFER_EMPTY, NULL };
    buffers[3] = { 0, SECBUFFER_EMPTY, NULL };
    SecBufferDesc desc = { SECBUFFER_VERSION, 4, buffers };

    SECURITY_STATUS status = DecryptMessage(&conn->ctx, &desc, 0, NULL);

    if (status == SEC_E_INCOMPLETE_MESSAGE){
      int n = raw_recv(conn->tcp.conn.handle, conn->raw_buf + conn->raw_len, sizeof(conn->raw_buf) - conn->raw_len);
      if (n <= 0){ return 0; }
      conn->raw_len += n;
      continue;
    }
    if (status == SEC_I_CONTEXT_EXPIRED){ return 0; }
    if (status != SEC_E_OK){
      printf("DecryptMessage failed: 0x%lx\n", status);
      return 0;
    }

    SecBuffer *data_buf = NULL, *extra_buf = NULL;
    for (int i = 0; i < 4; ++i){
      if (buffers[i].BufferType == SECBUFFER_DATA && !data_buf){ data_buf = &buffers[i]; }
      if (buffers[i].BufferType == SECBUFFER_EXTRA){ extra_buf = &buffers[i]; }
    }

    if (data_buf && data_buf->cbBuffer > 0){
      memcpy(conn->plain_buf, data_buf->pvBuffer, data_buf->cbBuffer);
      conn->plain_len = data_buf->cbBuffer;
      conn->plain_pos = 0;
    }

    conn->raw_len = 0;
    if (extra_buf && extra_buf->cbBuffer > 0){
      memmove(conn->raw_buf, extra_buf->pvBuffer, extra_buf->cbBuffer);
      conn->raw_len = extra_buf->cbBuffer;
    }

    if (conn->plain_len > 0){
      int avail = conn->plain_len;
      int n = (len < avail) ? len : avail;
      memcpy(buf, conn->plain_buf, n);
      conn->plain_pos = n;
      if (conn->plain_pos == conn->plain_len){ conn->plain_len = 0; conn->plain_pos = 0; }
      return n;
    }
  }
}

void tls_close(TlsConn *conn){
  DWORD shutdown_type = SCHANNEL_SHUTDOWN;
  SecBuffer buf = { sizeof(shutdown_type), SECBUFFER_TOKEN, &shutdown_type };
  SecBufferDesc desc = { SECBUFFER_VERSION, 1, &buf };
  ApplyControlToken(&conn->ctx, &desc);

  SecBuffer out_buf = { 0, SECBUFFER_TOKEN, NULL };
  SecBufferDesc out_desc = { SECBUFFER_VERSION, 1, &out_buf };
  DWORD ctx_attr;
  TimeStamp expiry;
  DWORD ctx_req = ISC_REQ_SEQUENCE_DETECT | ISC_REQ_REPLAY_DETECT |
                  ISC_REQ_CONFIDENTIALITY | ISC_REQ_ALLOCATE_MEMORY | ISC_REQ_STREAM;
  SECURITY_STATUS status = InitializeSecurityContextA(NULL, &conn->ctx, NULL, ctx_req, 
                                                      0, 0, NULL, 0, NULL, &out_desc, 
                                                      &ctx_attr, &expiry);
  if (status == SEC_E_OK && out_buf.cbBuffer > 0){
    raw_send(conn->tcp.conn.handle, (char*)out_buf.pvBuffer, out_buf.cbBuffer);
    FreeContextBuffer(out_buf.pvBuffer);
  }

  DeleteSecurityContext(&conn->ctx);
  tcp_close(&conn->tcp.conn);
}



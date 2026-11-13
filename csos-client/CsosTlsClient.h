#pragma once
#include "DiagnosticSerial.h"
#include <NetworkClientSecure.h>

// Arduino 3.2.1 prepares TCP, CA bundle, RNG and SNI before its deferred
// handshake. Rebuild only the SSL context so config stays immutable once
// attached to a context; reuse the core's authenticated handshake/cleanup.
class CsosTlsClient : public NetworkClientSecure {
public:
 using NetworkClientSecure::connect;
 int connect(const char* host,uint16_t port,int32_t timeout) override {
  if(_use_insecure)return fail(-1);
  setPlainStart();
  if(!NetworkClientSecure::connect(host,port,timeout))return fail(-1);
  mbedtls_ssl_free(&sslclient->ssl_ctx);
  mbedtls_ssl_init(&sslclient->ssl_ctx);
  int result=mbedtls_ssl_conf_max_frag_len(&sslclient->ssl_conf,MBEDTLS_SSL_MAX_FRAG_LEN_4096);
  if(result!=0)return fail(result);
  result=mbedtls_ssl_set_hostname(&sslclient->ssl_ctx,host);
  if(result!=0)return fail(result);
  result=mbedtls_ssl_setup(&sslclient->ssl_ctx,&sslclient->ssl_conf);
  if(result!=0)return fail(result);
  mbedtls_ssl_set_bio(&sslclient->ssl_ctx,&sslclient->socket,mbedtls_net_send,mbedtls_net_recv,nullptr);
  result=ssl_starttls_handshake(sslclient.get());
  if(result<0)return fail(result);
  _stillinPlainStart=false;
  if(mbedtls_ssl_get_verify_result(&sslclient->ssl_ctx)!=0)return fail(-1);
  const int limit=mbedtls_ssl_get_max_in_record_payload(&sslclient->ssl_ctx);
  CSOS_LOGF("[TLS] requested_mfl=4096 negotiated_in=%d verified=1 peer_default=%d\n",limit,int(limit>4096));
  return 1;
 }
private:
 int fail(int error){
  sslclient->last_error=error;_stillinPlainStart=false;stop();
  CSOS_LOGF("[TLS] requested_mfl=4096 error=%d verified=0\n",error);
  return 0;
 }
};

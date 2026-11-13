#include "CloudControl.h"
#include "CxlWssStream.h"
#include "CaBundle.h"
#include <WiFi.h>
#include <NetworkClientSecure.h>
#include <HTTPClient.h>
#include <cJSON.h>
#include <mbedtls/md.h>
#include <time.h>
#include <cstring>
namespace {
QueueHandle_t requests=nullptr,results=nullptr;
DesktopConnector desktopConnector=nullptr;
// One bounded reply, one TLS connection at a time. Never log response bodies.
class LimitedBody:public Stream {
public:
 String value;bool overflow=false;
 int available()override{return 0;}int read()override{return -1;}int peek()override{return -1;}void flush()override{}
 size_t write(uint8_t byte)override{return write(&byte,1);}
 size_t write(const uint8_t* p,size_t n)override{
  if(n>24576-value.length()){overflow=true;return n;}
  value.concat(reinterpret_cast<const char*>(p),n);return n;
 }
};
bool clockReady(){
 if(time(nullptr)>=1767225600)return true;
 configTime(0,0,"pool.ntp.org","time.cloudflare.com");
 const uint32_t start=millis();
 while(time(nullptr)<1767225600&&millis()-start<15000)delay(20);
 return time(nullptr)>=1767225600;
}
bool validName(const char* p){
 if(!p[0])return false;
 for(;*p;p++)if(!((*p>='a'&&*p<='z')||(*p>='A'&&*p<='Z')||(*p>='0'&&*p<='9')||*p=='-'))return false;
 return true;
}
void github(const CloudJob& job,CloudResult& out){
 if(!job.token[0]||!validName(job.name)){strcpy(out.detail,"Configura accesso GitHub");return;}
 if(!clockReady()){strcpy(out.detail,"Ora TLS non disponibile");return;}
 NetworkClientSecure tls;tls.setCACertBundle(CSOS_CA_BUNDLE,sizeof(CSOS_CA_BUNDLE));tls.setHandshakeTimeout(10);
 HTTPClient http;http.setConnectTimeout(8000);http.setTimeout(8000);http.setReuse(false);
 http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
 String url="https://api.github.com/user/codespaces/"+String(job.name);
 if(job.kind==CloudJobKind::Start)url+="/start";
 if(job.kind==CloudJobKind::Stop)url+="/stop";
 if(!http.begin(tls,url)){strcpy(out.detail,"GitHub: inizializzazione TLS fallita");return;}
 http.addHeader("Authorization","Bearer "+String(job.token));
 http.addHeader("Accept","application/vnd.github+json");
 http.addHeader("X-GitHub-Api-Version","2026-03-10");
 http.addHeader("User-Agent","CS-OS-Cardputer");
 const char* responseHeaders[]={"Retry-After","x-ratelimit-remaining","x-ratelimit-reset"};
 http.collectHeaders(responseHeaders,3);
 if(job.kind==CloudJobKind::Stop){
  if(WiFi.status()!=WL_CONNECTED||WiFi.SSID()!=job.stopNetwork){
   strcpy(out.detail,"Rete cambiata; arresto non inviato");return;
  }
  if(!PreDesktopModel::fresh(millis(),job.stopStatusAt)){
   strcpy(out.detail,"Stato scaduto; arresto non inviato");return;
  }
 }
 out.http=job.kind==CloudJobKind::Status?http.GET():http.POST("");
 if(out.http==403||out.http==429){
  const long retrySeconds=http.header("Retry-After").toInt();
  const long resetAt=http.header("x-ratelimit-reset").toInt();
  const bool exhausted=http.header("x-ratelimit-remaining")=="0";
  const int64_t resetDelay=int64_t(resetAt)-int64_t(time(nullptr));
  int64_t seconds=retrySeconds>0?retrySeconds:0;
  if(exhausted&&resetDelay>seconds)seconds=resetDelay;
  out.retryAfterMs=uint32_t(std::min<int64_t>(seconds,2147483)*1000);
  out.rateLimited=out.http==429||exhausted||retrySeconds>0;
  if(out.http==403&&!out.rateLimited){
   LimitedBody body;
   if(http.writeToStream(&body)>=0&&!body.overflow){
    cJSON* denied=cJSON_Parse(body.value.c_str());
    const cJSON* message=denied?cJSON_GetObjectItemCaseSensitive(denied,"message"):nullptr;
    if(cJSON_IsString(message)&&message->valuestring){
     String reason=message->valuestring;reason.toLowerCase();
     out.rateLimited=reason.indexOf("rate limit")>=0;
     out.authRejected=!out.rateLimited&&reason.startsWith("resource not accessible");
    }
    if(denied)cJSON_Delete(denied);
   }
  }
 }
 if(out.http>=200&&out.http<300){
  if(job.kind!=CloudJobKind::Status){out.ok=true;strcpy(out.detail,"Richiesta accettata; verifico lo stato");}
  else{
   LimitedBody body;const int written=http.writeToStream(&body);
   if(written>=0&&!body.overflow){
    cJSON* json=cJSON_Parse(body.value.c_str());
    const cJSON* state=json?cJSON_GetObjectItemCaseSensitive(json,"state"):nullptr;
    if(cJSON_IsString(state)&&state->valuestring){
     const char* s=state->valuestring;out.ok=true;
     if(!strcmp(s,"Available"))out.state=GithubState::Available;
     else if(!strcmp(s,"Shutdown"))out.state=GithubState::Stopped;
     else if(!strcmp(s,"Starting")||!strcmp(s,"Provisioning")||!strcmp(s,"Rebuilding")||!strcmp(s,"Updating"))out.state=GithubState::Starting;
     else if(!strcmp(s,"ShuttingDown"))out.state=GithubState::Stopping;
     snprintf(out.detail,sizeof(out.detail),"GitHub: %.45s",s);
    }
    if(json)cJSON_Delete(json);
   }
   if(!out.ok)strcpy(out.detail,"GitHub: risposta non interpretabile");
  }
 }else if(out.http==401)strcpy(out.detail,"GitHub 401: token non accettato");
 else if(out.http==403)strcpy(out.detail,out.rateLimited?"GitHub: limite API; verifica rinviata":out.authRejected?"GitHub 403: permessi insufficienti":"GitHub 403: causa non verificata");
 else if(out.http==404)strcpy(out.detail,"GitHub 404: nome o accesso da verificare");
 else if(out.http==429)strcpy(out.detail,"GitHub: limite richieste; attendo");
 else if(out.http>0)snprintf(out.detail,sizeof(out.detail),"GitHub HTTP %d",out.http);
 else strcpy(out.detail,"GitHub: rete/TLS o tempo scaduto");
 http.end();tls.stop();
}
bool exact(CxlWssStream& stream,uint8_t* data,size_t len,uint32_t timeout){
 const uint32_t start=millis();size_t got=0;
 while(got<len&&stream.connected()&&millis()-start<timeout){
  const int available=stream.available();
  if(available>0){const int n=stream.read(data+got,std::min<size_t>(available,len-got));if(n>0)got+=n;}
  else delay(1);
 }
 return got==len;
}
void probe(const CloudJob& job,CloudResult& out){
 // Persistent object avoids leaking its queue on repeated probes.
 static CxlWssStream stream;
 stream.claimOwner();stream.setCredentials(job.user,job.password);
 if(!stream.connect()){
  out.transport=stream.failure();snprintf(out.detail,sizeof(out.detail),"%s",transportFailureText(out.transport));stream.stop();return;
 }
 uint8_t hello[21]={};
 if(!exact(stream,hello,sizeof(hello),3000)||memcmp(hello,"CXLS",4)||hello[4]!=3){strcpy(out.detail,"CXL: protocollo non verificato");stream.stop();return;}
 uint8_t reply[45]={'C','X','L','R',3,64,1,240,0}; //320x240, same receiver budget
 constexpr uint32_t jpegMax=16384;
 reply[9]=jpegMax;reply[10]=jpegMax>>8;reply[11]=jpegMax>>16;reply[12]=jpegMax>>24;
 mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256),reinterpret_cast<const uint8_t*>(job.pin),strlen(job.pin),hello+5,16,reply+13);
 stream.write(reply,sizeof(reply));uint8_t verdict=0;
 if(exact(stream,&verdict,1,4000)&&verdict=='O'){
  uint8_t info[5]={};
  if(exact(stream,info,5,2000)){
   uint8_t name[255];
   out.ok=!info[4]||exact(stream,name,info[4],2000);
  }
  strcpy(out.detail,out.ok?"CXL autenticato; Entra per collegare":"CXL: risposta incompleta");
 }else if(verdict=='X'){
  uint8_t reason=0;exact(stream,&reason,1,500);out.authRejected=reason==1;
  snprintf(out.detail,sizeof(out.detail),"CXL: accesso rifiutato (%u)",reason);
 }
 else strcpy(out.detail,"CXL: autenticazione non completata");
 // No keyboard/mouse, presence lease or frame-ready claim from a probe.
 stream.stop();
}
void worker(void*){
 CloudJob job;
 for(;;)if(xQueueReceive(requests,&job,portMAX_DELAY)==pdTRUE){
  CloudResult result;result.kind=job.kind;result.generation=job.generation;
  if(job.kind==CloudJobKind::Connect){
   if(desktopConnector){result.connection=desktopConnector();result.ok=result.connection==CONN_OK;}
   else strcpy(result.detail,"Collegamento desktop non inizializzato");
  }
  else if(WiFi.status()!=WL_CONNECTED)strcpy(result.detail,"Wi-Fi non connesso");
  else if(job.kind==CloudJobKind::Probe)probe(job,result);else github(job,result);
  memset(job.token,0,sizeof(job.token));memset(job.password,0,sizeof(job.password));memset(job.pin,0,sizeof(job.pin));
  xQueueOverwrite(results,&result);
 }
}
}
void setDesktopConnector(DesktopConnector connector){desktopConnector=connector;}
bool beginCloudControl(){
 if(requests&&results)return true;
 requests=xQueueCreate(1,sizeof(CloudJob));results=xQueueCreate(1,sizeof(CloudResult));
 if(!requests||!results)return false;
 return xTaskCreatePinnedToCore(worker,"csos-control",12288,nullptr,1,nullptr,0)==pdPASS;
}
bool submitCloudJob(const CloudJob& job){return requests&&xQueueSend(requests,&job,0)==pdTRUE;}
bool pollCloudResult(CloudResult& result){return results&&xQueueReceive(results,&result,0)==pdTRUE;}

#pragma once
#include <cstdint>
#include "PreDesktopModel.h"
#include "TransportStatus.h"
enum ConnResult { CONN_OK, CONN_FAIL, CONN_BADPIN, CONN_CANCELLED };
using DesktopConnector=ConnResult(*)();
enum class CloudJobKind:uint8_t {Status,Start,Stop,Probe,Connect};
struct CloudJob {
 CloudJobKind kind=CloudJobKind::Status;uint32_t generation=0;
 char token[256]={},name[81]={},user[65]={},password[129]={},pin[65]={};
 char stopNetwork[33]={};
 uint32_t stopStatusAt=0;
};
struct CloudResult {
 CloudJobKind kind=CloudJobKind::Status;uint32_t generation=0;
 ConnResult connection=CONN_FAIL;
 int http=0;GithubState state=GithubState::Unknown;
 bool ok=false,authRejected=false;TransportFailure transport=TransportFailure::None;
 uint32_t retryAfterMs=0;bool rateLimited=false;
 char detail[80]={};
};
bool beginCloudControl();
// Register before submitting Connect. The callback runs on the existing worker.
void setDesktopConnector(DesktopConnector connector);
bool submitCloudJob(const CloudJob& job);
bool pollCloudResult(CloudResult& result);

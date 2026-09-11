// Real web.cpp handlers are included below; only hardware/network interfaces are fake.
#include <cassert>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <string>
#include <map>
#include <vector>
#include "ota_session.h"
#include "json.h"
#define ESP8266
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
#define F(s) s
#define PSTR(s) s
#define TAKE_MUTEX()
#define GIVE_MUTEX()
#define snprintf_P snprintf
#define U_FLASH 0
using String = std::string;
using StreamString = std::string;
size_t strlcpy(char *dst, const char *src, size_t n) {
    auto len = strlen(src); if(n) {auto used = len<n-1?len:n-1;memcpy(dst,src,used);dst[used]=0;} return len;
}
uint32_t nowMs;
uint32_t _millis() {return nowMs;}
unsigned restarts, shutdowns, mdnsSends, socketStops;
bool mdnsOpen = true, ebootPending = false, suspend_service_loop = false, homekit_setup_done = true;
void shutdown_comms() {++shutdowns;}
void arduino_homekit_close() {mdnsOpen=false;}
void sync_and_restart() {++restarts;}
void delay(unsigned) {}
void eboot_command_clear() {ebootPending=false;}
struct {void announce() {assert(mdnsOpen);++mdnsSends;}} MDNS;
struct Ticker {
    template<class F> void once_ms(uint32_t,F) {}
    void detach() {}
};
template<class F> void schedule_recurrent_function_us(F fn,int) {fn();}
struct Client {
    void stop() {++socketStops;}
    bool connected() const {return true;}
    void setNoDelay(bool) {}
};
struct Subscription {Client client;};
Subscription *firmwareUpdateSub = nullptr;
struct {
    template<class T> void print(T) {}
} Serial;
struct {
    uint32_t space=1298432;
    uint32_t getFreeSketchSpace() {return space;}
    int getFlashChipSpeed() {return 40000000;}
} ESP;
struct Config {
    bool getPasswordRequired() {return true;}
    const char* getwwwUsername() {return "admin";}
    const char* getwwwCredentials() {return "";}
} config, *userConfig=&config;
enum UploadStatus {UPLOAD_FILE_START, UPLOAD_FILE_WRITE, UPLOAD_FILE_END, UPLOAD_FILE_ABORTED};
struct HTTPUpload {
    UploadStatus status=UPLOAD_FILE_START;
    String filename="firmware.bin";
    size_t currentSize=0,totalSize=0;
    uint8_t buf[2048]{};
};
struct Server {
    std::map<std::string,std::string> query;
    HTTPUpload upload_;
    int response=0;
    bool authenticated=true;
    std::string body;
    const String& arg(const char *key) {return query[key];}
    size_t args() {return query.size();}
    HTTPUpload& upload() {return upload_;}
    Client client() {return {};}
    bool authenticateDigest(const char*,const char*) {return authenticated;}
    void sendHeader(const char*,const char*) {}
    void send(int code,const char*,const char* data) {response=code;body=data;}
    void send_P(int code,const char* type,const char* data) {send(code,type,data);}
    void stop() {}
} server;
bool requestAuthenticated() {return server.authenticated;}
struct FakeUpdate {
    bool running=false, failBegin=false, failWrite=false, failEnd=false;
    unsigned begins=0,ends=0;
    bool begin(size_t,int) {++begins;running=!failBegin;return running;}
    bool isRunning() {return running;}
    bool end(bool finalize=false) {++ends;running=false; if(!failEnd) ebootPending=true;return !failEnd;}
    bool setMD5(const char*) {return true;}
    size_t write(uint8_t*,size_t size) {return failWrite?0:size;}
    void printError(StreamString& out) {out="injected updater error";}
} Update;
char firmwareMD5[36]{};
size_t firmwareSize=0;
char statusBuffer[2048]{}, *status_json=statusBuffer, writeBuffer[512]{};
bool clientWrite(Client,const char*) {return true;}
const char *type_txt="text/plain";
bool _authenticatedUpdate;
std::string _updaterError;
static OtaSession otaSession;
static Ticker uploadIdleTimer;
static Client firmwareUploadClient;
// Generated verbatim from src/web.cpp by run.py, including the real MDNS guard.
#include "ota_handlers.inc"

void reset() {
    otaSession=OtaSession();server=Server();Update=FakeUpdate();
    config=Config();userConfig=&config;nowMs=0;restarts=shutdowns=mdnsSends=socketStops=0;
    mdnsOpen=true;ebootPending=false;suspend_service_loop=false;homekit_setup_done=true;
    _updaterError.clear();firmwareMD5[0]=0;firmwareSize=0;
    server.query={{"action","update"},{"size","4096"},{"md5",std::string(32,'a')}};
}
void event(UploadStatus status,size_t bytes=0) {
    server.upload_.status=status;server.upload_.currentSize=bytes;
    if(status==UPLOAD_FILE_WRITE)server.upload_.totalSize+=bytes;
    handle_firmware_upload();
}
void assertRecovery() {
    assert(otaSession.recovering());assert(!ebootPending);
    announce_mdns();assert(mdnsSends==0);
    poll_ota_recovery();assert(restarts==0);
    nowMs+=1499;poll_ota_recovery();assert(restarts==0);
    nowMs+=1;poll_ota_recovery();assert(restarts==1);
}
int main() {
    reset();announce_mdns();assert(mdnsSends==1);
    reset();event(UPLOAD_FILE_START);assert(shutdowns==1&&!mdnsOpen);
    event(UPLOAD_FILE_ABORTED);assertRecovery(); // includes abort with zero data
    reset();nowMs=UINT32_MAX-500;event(UPLOAD_FILE_START);event(UPLOAD_FILE_WRITE,2048);
    event(UPLOAD_FILE_ABORTED);assertRecovery(); // millis wraps during deferred reboot
    reset();Update.failBegin=true;event(UPLOAD_FILE_START);assertRecovery();
    reset();event(UPLOAD_FILE_START);Update.failWrite=true;event(UPLOAD_FILE_WRITE,2048);
    handle_update();assert(server.response==400);assertRecovery();
    reset();event(UPLOAD_FILE_START);event(UPLOAD_FILE_WRITE,4096);Update.failEnd=true;
    event(UPLOAD_FILE_END);handle_update();assert(server.response==400);assertRecovery();
    reset();event(UPLOAD_FILE_START);event(UPLOAD_FILE_WRITE,2048);event(UPLOAD_FILE_END);
    handle_update();assert(server.response==400);assertRecovery(); // truncated body
    reset();event(UPLOAD_FILE_START);event(UPLOAD_FILE_WRITE,4096);event(UPLOAD_FILE_END);
    handle_update();assert(server.response==200&&ebootPending);assert(otaSession.complete());
    nowMs=180000;announce_mdns();poll_ota_recovery();assert(!mdnsSends&&!restarts);
    // Failed requests must clear even an accidentally staged complete image.
    event(UPLOAD_FILE_ABORTED);assertRecovery();
    for(const char *bad : {"-1","12garbage","4294967296"}) {
        reset();server.query["size"]=bad;event(UPLOAD_FILE_START);handle_update();
        assert(server.response==400&&!shutdowns&&!otaSession.recovering());
    }
    reset();server.query["size"]="2000000";event(UPLOAD_FILE_START);handle_update();
    assert(server.response==400&&!shutdowns&&!Update.begins);
    reset();server.query["md5"]="bad";event(UPLOAD_FILE_START);handle_update();
    assert(server.response==400&&!shutdowns);
    reset();server.authenticated=false;event(UPLOAD_FILE_START);assert(!shutdowns&&!Update.begins);
    reset();handle_update();assert(server.response==400&&!restarts); // no file
    reset();event(UPLOAD_FILE_START);event(UPLOAD_FILE_START);assertRecovery(); // double shutdown rejected
    // Metadata failure does not poison a subsequent valid attempt.
    reset();server.query["md5"]="bad";event(UPLOAD_FILE_START);
    server.query["md5"]=std::string(32,'b');event(UPLOAD_FILE_START);assert(shutdowns==1);
    // No stale digest inherited by a legacy uploader.
    reset();strcpy(firmwareMD5,std::string(32,'a').c_str());server.query.erase("md5");
    event(UPLOAD_FILE_START);assert(firmwareMD5[0]==0);
    reset();event(UPLOAD_FILE_START);Update.failWrite=true;event(UPLOAD_FILE_WRITE,2048);
    nowMs=30000;check_upload_timeout();assert(socketStops==1); // parser stalled after write error
    event(UPLOAD_FILE_ABORTED);poll_ota_recovery();assert(restarts==1);
    reset();event(UPLOAD_FILE_START);nowMs=29999;check_upload_timeout();assert(!socketStops);
    event(UPLOAD_FILE_WRITE,2048);nowMs+=29999;check_upload_timeout();assert(!socketStops);
    nowMs+=1;check_upload_timeout();assert(socketStops==1&&!restarts);
    event(UPLOAD_FILE_ABORTED);assertRecovery();
    reset();event(UPLOAD_FILE_START);event(UPLOAD_FILE_WRITE,4096);event(UPLOAD_FILE_END);
    server.query["action"]="verify";event(UPLOAD_FILE_START);event(UPLOAD_FILE_ABORTED);
    assert(ebootPending&&!otaSession.recovering()); // verification must not cancel a staged image
    puts("OTA: abort, begin/write/finalize failures, metadata, rollover, MDNS after shutdown: passed");
}

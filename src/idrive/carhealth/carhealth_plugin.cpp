// libNbtCarHealth.so - "Car health" entry under Vehicle Info (HMI plugin, /var/opt/hmi/data/rw/Plugins/NbtCarHealth).
//
// Opening the entry shows page 10: up to 24 live rows that nbt_health (src/health/nbt_health.c, its own process started by
// boot.sh) writes to /fs/ram3/nbt-health.txt: coolant, battery, engine, intake air, boost, odometer, faults. While the
// page has focus this plugin refreshes the rows every second and touches /fs/ram3/nbt-health-active, which is what makes
// nbt_health talk to the car; nothing is asked of the car while the page is closed. Separate from the Android Auto plugin
// so neither can break the other.
//
// Rows are raDataModel labels filled with updateModel(id, const char*), exported by this car's libAppFramework
// (swfl_00001289). Guard: /fs/ram3/nbt-health-trying exists while the rows are pushed for the first time in an HMI
// start; if iDrive restarts during that, the next load finds it and leaves the page empty for the rest of the boot.
// Disable for good: touch /var/opt/hmi/data/rw/NbtAndroidAutoRT/health.off. Log: /fs/ram3/nbt-carhealth.log.
// ABI as aa_entry_plugin.cpp and valinet's NbtReboot plugin (car-tested PluginApp ctor, addHandler(state, 1 = focus)).
#include "../nbt_image_abi.hpp"
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <new>
namespace bmw {
class IDispatcher; class Bundle; class IPluginApp;
class Parcel { public: bool readBool(bool&) const; };
template<class R,class A1,class A2,class A3,class A4> struct Delegate {
    void* object;
    R (*stub)(void*,A1);
};
typedef Delegate<bool,const Parcel&,void,void,void> ActionDelegate;
class PluginApp {
public:
    PluginApp(IDispatcher&,BasicString<char>,const Bundle&,const Bundle&);
    ~PluginApp();
    void post(Runnable*,unsigned);
    void updateModel(unsigned,const char*);   // _ZN3bmw9PluginApp11updateModelEjPKc (this car's swfl_00001289)
    void addHandler(unsigned,unsigned,const ActionDelegate&);
    bool start();
private:
    uint32_t opaque_[1024];
};
}
namespace {
const unsigned kPage=10, kFocus=1, kVisible=11, kRow0=111, kRows=24, kLen=48;
// car 2026-11-22: leaving the page with MENU sent no focus=false, so polling ran on (and nbt_health kept the car's
// tester address busy). Polling now also stops when the page stops being visible, and after kMaxPoll seconds.
const long kMaxPoll=600;
const char* const kActive="/fs/ram3/nbt-health-active";
const char* const kOut="/fs/ram3/nbt-health.txt";
const char* const kTrying="/fs/ram3/nbt-health-trying";
const char* const kOff="/var/opt/hmi/data/rw/NbtAndroidAutoRT/health.off";
const char* const kLog="/fs/ram3/nbt-carhealth.log";

bmw::PluginApp* g_app=0;
bool g_off=false, g_pushedOnce=false;
long g_openedAt=0;
char g_shown[kRows][kLen];

long uptime_s() { struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return (long)t.tv_sec; }
void plog(const char* what) {
    int fd=open(kLog,O_WRONLY|O_CREAT|O_APPEND,0644);
    if(fd<0)return;
    char line[128]; int n=snprintf(line,sizeof(line),"%ld carhealth: %s\n",uptime_s(),what);
    if(n>0&&write(fd,line,(size_t)n)<0){}
    close(fd);
}

void readRows(char rows[kRows][kLen]) {
    memset(rows,0,kRows*kLen);
    struct stat st;
    if(stat(kOut,&st)!=0||time(0)-st.st_mtime>15){
        // nbt_health answers within ~5 s of the page opening; after 12 s without a fresh file say so plainly
        const bool slow=uptime_s()-g_openedAt>12;
        if(access(kOut,F_OK)==0) snprintf(rows[0],kLen,"%s",slow?"No new data from the car":"Reading the car ...");
        else snprintf(rows[0],kLen,"%s",slow?"Health service not running":"Starting ...");
        if(slow) snprintf(rows[1],kLen,"%s","Restart iDrive; if it stays,");
        if(slow) snprintf(rows[2],kLen,"%s","pull the logs (nbt-health.err)");
        return;
    }
    char buf[kRows*kLen+16];
    int fd=open(kOut,O_RDONLY); if(fd<0)return;
    ssize_t n=read(fd,buf,sizeof(buf)-1); close(fd);
    if(n<=0)return;
    unsigned row=0,col=0;
    for(ssize_t i=0;i<n&&row<kRows;++i){
        char c=buf[i];
        if(c=='\n'){++row;col=0;continue;}
        if(c>=' '&&c<127&&col<kLen-1)rows[row][col++]=c;
    }
}

class Poll : public bmw::Runnable {
public:
    bool active, pending;
    Poll():active(false),pending(false) {}
    void run() {
        pending=false;
        if(!active||!g_app||g_off)return;
        if(uptime_s()-g_openedAt>kMaxPoll){active=false;plog("stopped: open longer than 10 min");return;}
        int fd=open(kActive,O_WRONLY|O_CREAT|O_TRUNC,0644);
        if(fd>=0){if(write(fd,"1",1)<0){}close(fd);}
        char rows[kRows][kLen];
        readRows(rows);
        if(!g_pushedOnce){int t=open(kTrying,O_WRONLY|O_CREAT|O_TRUNC,0644);if(t>=0)close(t);}
        for(unsigned i=0;i<kRows;++i){
            if(strcmp(rows[i],g_shown[i])==0)continue;
            g_app->updateModel(kRow0+i,rows[i][0]?rows[i]:" ");
            memcpy(g_shown[i],rows[i],kLen);
        }
        if(!g_pushedOnce){g_pushedOnce=true;unlink(kTrying);plog("rows shown");}
        pending=true;g_app->post(this,1000);
    }
};
Poll g_poll;

bool pageVisible(void*,const bmw::Parcel& p) {
    bool visible=false;p.readBool(visible);
    plog(visible?"page visible":"page hidden");
    if(!visible)g_poll.active=false;
    return true;
}

bool pageFocus(void*,const bmw::Parcel& p) {
    bool focused=false;p.readBool(focused);
    plog(focused?"page focus":"page left");
    g_poll.active=focused&&!g_off;
    if(g_poll.active){
        g_openedAt=uptime_s();
        for(unsigned i=0;i<kRows;++i){g_shown[i][0]=1;g_shown[i][1]=0;}   // push every row again
        if(!g_poll.pending&&g_app){g_poll.pending=true;g_app->post(&g_poll,100);}
    }
    return true;
}

// a null delegate object makes the HMI skip the handler (NbtReboot, car 2026-11-12)
int g_handlerObject=0;
bmw::IPluginApp* create(bmw::IDispatcher& d,const char* name,const bmw::Bundle& p,const bmw::Bundle& c) {
    if(g_app)return 0;
    g_app=new bmw::PluginApp(d,bmw::BasicString<char>(name),p,c);
    g_poll.active=false;g_poll.pending=false;g_pushedOnce=false;
    if(access(kTrying,F_OK)==0){g_off=true;unlink(kTrying);plog("iDrive restarted while showing the rows: off for this boot");}
    if(access(kOff,F_OK)==0){g_off=true;plog("off (health.off)");}
    bmw::ActionDelegate onPage={&g_handlerObject,&pageFocus};
    g_app->addHandler(kPage,kFocus,onPage);
    bmw::ActionDelegate onVisible={&g_handlerObject,&pageVisible};
    g_app->addHandler(kPage,kVisible,onVisible);   // type 11 = VISIBLE, as the Android Auto plugin's hold pages use it
    plog(g_app->start()?"plugin created, started":"plugin created");
    return reinterpret_cast<bmw::IPluginApp*>(g_app);
}
void destroy(bmw::IPluginApp* p) {
    if(!g_app)return;
    g_poll.active=false;
    delete reinterpret_cast<bmw::PluginApp*>(p);
    g_app=0;
}
}
struct PluginAppInfo {
    uint16_t major,minor;const char* name;const char* vendor;
    bmw::IPluginApp*(*create)(bmw::IDispatcher&,const char*,const bmw::Bundle&,const bmw::Bundle&);
    void(*destroy)(bmw::IPluginApp*);
};
typedef char InfoSize[(sizeof(PluginAppInfo)==20)?1:-1];
extern "C" { PluginAppInfo pluginAppInfo={1,0,"NbtCarHealth","NBT-Project",create,destroy}; }

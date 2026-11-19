// libNbtAndroidAuto.so - the "Android Auto" entry under Multimedia (HMI plugin, in the installed package).
//
// The picture is not drawn by this page: nbt_hwvideo shows the hardware video window in front of the HMI (the very
// first version used a WAL sprite, see experiments/wal-overlay; some comments below still say so). This page
//   - starts the connection: opening it creates /fs/ram3/nbt-aa-request (native_service waits for it with gate.mode)
//   - shows the connection status (from /fs/ram3/nbt-aa-state, written by native_service at least every 2 s):
//       no phone / phone found, starting / replug needed / device without Android Auto / several devices / service down
//   - brings Android Auto to the front as soon as it runs: iDrive is moved to the inert hold state 13 (focusEvent 52),
//     where controller turns / pushes / tilts do nothing in iDrive (nbt_knob forwards them to the phone), and the overlay
//     is shown (/fs/ram3/nbt-aa-hidden removed). Same idea as the NBT-AAD reference rhmiapp state 13 and iDrive's own
//     ST_CarPlay state.
//   - BACK (or a tilt iDrive treats as back) pops iDrive from 13 to the entry page 10: while Android Auto runs the press
//     belonged to the phone, so iDrive is put straight back on 13 (the presenter waits 300 ms before hiding: no blink).
//   - MENU / MEDIA / NAV / COM / MAP / OPTION leave the page the iDrive way: the overlay hides, the session keeps running
//     in the background. Multimedia -> Android Auto then brings the running session to the front at once.
// Hold states 13..20 are nested (see "hold states" below). Loop guard: at most 10 hold events per minute.
// ABI as in experiments/png-pipeline/dynamic_image_plugin.cpp (vehicle-tested PluginApp ctor, addHandler(state,1,...) focus events, post()).
// Checked in this car's libraries: PluginApp::triggerEvent(unsigned), updateModel(unsigned, TextID const&),
// TextID(unsigned) = ID(id, 0x28) (libCoreFramework.A.so); focusEvent/parentId are known to this HmiMain3D.
#include "nbt_image_abi.hpp"
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#ifdef NBT_OPEN_TOOLCHAIN
#include "qnx_spawn_compat.h"   // the open sysroot has no <spawn.h>
#else
#include <spawn.h>
#endif
#include <signal.h>
#include <pthread.h>
#include <new>
namespace bmw {
class IDispatcher; class Bundle; class IPluginApp;
class Parcel { public: bool readBool(bool&) const; };
template<class R,class A1,class A2,class A3,class A4> struct Delegate {
    void* object;
    R (*stub)(void*,A1);
};
typedef Delegate<bool,const Parcel&,void,void,void> ActionDelegate;
typedef Delegate<bool,const BasicString<unsigned short>&,void,void,void> CdsDelegate;
class TextID {
public:
    explicit TextID(unsigned id);   // _ZN3bmw6TextIDC1Ej -> ID(id, 0x28): {u32 id, u32 type}, trivially destructible
private:
    uint32_t storage_[4];           // 8 bytes used, extra room is harmless
};
class PluginApp {
public:
    PluginApp(IDispatcher&,BasicString<char>,const Bundle&,const Bundle&);
    ~PluginApp();
    void post(Runnable*,unsigned);
    void triggerEvent(unsigned);
    void updateModel(unsigned,const TextID&);
    void addHandler(unsigned,unsigned,const ActionDelegate&);
    // car data (CDS) property change handler; this car's libAppFramework.A.so (valinet/nbt-android-auto, 2026-11-17)
    void cdsAddPropertyChangedEventHandler(BasicString<char>&,unsigned,const CdsDelegate&);
private:
    uint32_t opaque_[1024];
};
}
namespace {
const char* const kRequest="/fs/ram3/nbt-aa-request";
const char* const kHidden="/fs/ram3/nbt-aa-hidden";
const char* const kState="/fs/ram3/nbt-aa-state";
const char* const kLeave="/fs/ram3/nbt-aa-leave";
const char* const kResume="/fs/ram3/nbt-aa-resume";      // nbt_hwvideo: the camera (or another video source) is gone
const unsigned kEntryState=10, kHoldState=13, kEvHold=52, kFocus=1, kVisible=11, kStatusModel=4;
// picture mode selection (2026-11-08): buttons on page 10 lead to the inert states 30 (16:9) / 31 (widescreen); their
// focus handler stores the mode, restarts the service and sends focus event 53 back to page 10
const unsigned kMode169State=30, kModeWideState=31, kEvEntry=53, kModel169=6, kModelWide=7;
const unsigned kRestartState=32;                     // "Android Auto neu starten" -> inert page 32, back to 10
const unsigned kEvHoldAlt=54;
// Why iDrive "ignored" 52 / 54 for the rest of a drive (found by valinet/nbt-android-auto, car 2026-11-18, HmiMain3D
// CFocusEvent::perform): every focus event object counts its performs and refuses from the 12th on ("FocusEvent counter
// > 10", never reset until the HMI restarts). Our reported bug "Android Auto shows connected but does not open, a restart
// of the service does not help" is exactly that. So the XML has a pool of 500 focus events (200..699, all to the deepest
// hold state 20, like 52); hold() uses each 10 times, then the next, and moves on at once after an ignored one.
const unsigned kEvPoolFirst=200, kEvPoolCount=500, kEvPoolUses=10;                        // focus event to hold state 13 (fallback when 52 is ignored)
// text ids in NbtAndroidAuto_ALL_Texts.zip (make_aa_entry_resources.py)
enum { kTxtConnectUsb=2, kTxtNoPhone=3, kTxtStarting=4, kTxtRunning=5, kTxtReplug=6, kTxtUnsupported=7,
       kTxtServiceDown=8, kTxtSeveral=9, kTxt169=11, kTxtWide=12, kTxt169On=16, kTxtWideOn=17 };
void touch(const char* path) { int fd=open(path,O_WRONLY|O_CREAT|O_TRUNC,0644); if(fd>=0)close(fd); }
// --- automatic start of the Android Auto service (boot.sh of the persistent package), like the NBT-AAD reference:
// QNX spawn() into a new process group, no zombie, stdin/out/err /dev/null. Rate-limited to one start per 30 s.
const char* const kBoot="/var/opt/hmi/data/rw/NbtAndroidAutoRT/boot.sh";
long g_lastLaunch=0;
long mono_s() { struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return (long)t.tv_sec; }
void launchService() {
    long t=mono_s();
    if(g_lastLaunch&&t-g_lastLaunch<30)return;
    g_lastLaunch=t;
    if(access(kBoot,R_OK)!=0)return;
    int nul=open("/dev/null",O_RDWR);
    if(nul<0)return;
    int fdmap[3]={nul,nul,nul};
    struct inheritance inh;
    memset(&inh,0,sizeof(inh));
    inh.flags=SPAWN_SETGROUP|SPAWN_SETSIGDEF|SPAWN_SETSIGMASK|SPAWN_NOZOMBIE;
    inh.pgroup=SPAWN_NEWPGROUP;
    sigfillset(&inh.sigdefault);
    sigemptyset(&inh.sigmask);
    char* argv[]={(char*)"/bin/sh",(char*)kBoot,0};
    char* envp[]={(char*)"PATH=/bin:/usr/bin:/sbin:/usr/sbin:/opt/sys/bin",0};
    spawn("/bin/sh",3,fdmap,&inh,argv,envp);
    close(nul);
}
// --- picture mode: flag file in the persistent package (read by boot.sh). split800 = "16:9" (1:1 at the left, iDrive
// split screen on the right), anamorph = "widescreen" (default when no flag exists). The service is restarted so the
// new mode applies; the phone negotiates its video format at the next connection.
const char* const kPkg="/var/opt/hmi/data/rw/NbtAndroidAutoRT";
bool modeIs169() { return access("/var/opt/hmi/data/rw/NbtAndroidAutoRT/split800",F_OK)==0; }
long g_lastMode=0;
// restart of the Android Auto service (not the head unit): stop native_service, wait for its lock to go, run boot.sh.
// flag = new picture mode flag file, or 0 to keep the mode (restart button, user request 2026-11-08: recovery when
// Android Auto shows "connected" but the picture does not come)
void restartService(const char* flag) {
    long t=mono_s();
    if(t<60||(g_lastMode&&t-g_lastMode<10))return;     // not right after boot (restored state) or in a burst
    g_lastMode=t;
    int nul=open("/dev/null",O_RDWR);
    if(nul<0)return;
    int fdmap[3]={nul,nul,nul};
    struct inheritance inh;
    memset(&inh,0,sizeof(inh));
    inh.flags=SPAWN_SETGROUP|SPAWN_SETSIGDEF|SPAWN_SETSIGMASK|SPAWN_NOZOMBIE;
    inh.pgroup=SPAWN_NEWPGROUP;
    sigfillset(&inh.sigdefault);
    sigemptyset(&inh.sigmask);
    char cmd[512];
    char modeCmd[96]="";
    if(flag)snprintf(modeCmd,sizeof(modeCmd),"rm -f anamorph center800 split800 classic; touch %s; ",flag);
    snprintf(cmd,sizeof(cmd),"cd %s || exit 1; %s"
             "slay -f -Q -s TERM native_service; i=0; while test -e /fs/ram3/nbt-aa-native/service.lock -a $i -lt 20; "
             "do sleep 1; i=$((i+1)); done; slay -f -Q -s KILL native_service; rm -f /fs/ram3/nbt-aa-native/service.lock; "
             "sh %s",kPkg,modeCmd,kBoot);
    char* argv[]={(char*)"/bin/sh",(char*)"-c",cmd,0};
    // restart.sh of the package (2026-11-09) does the same after a diagnostic snapshot; the inline command is the fallback
    const char* const script="/var/opt/hmi/data/rw/NbtAndroidAutoRT/restart.sh";
    char* argvScript[]={(char*)"/bin/sh",(char*)script,(char*)(flag?flag:""),0};
    char* envp[]={(char*)"PATH=/bin:/usr/bin:/sbin:/usr/sbin:/opt/sys/bin",0};
    spawn("/bin/sh",3,fdmap,&inh,access(script,R_OK)==0?argvScript:argv,envp);
    close(nul);
}
void setMode(bool split) { restartService(split?"split800":"anamorph"); }
// started once from create(): factory-time posts did not run on this HMI (2026-11-06), so a detached thread waits
void* autostartThread(void*) { sleep(15); launchService(); return 0; }
long now_s() { struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return (long)t.tv_sec; }
long long now_ms() { struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return (long long)t.tv_sec*1000+t.tv_nsec/1000000; }
// small diagnostic log (car 2026-11-07: after a while MENU no longer left Android Auto); bounded to 64 KB
void plog(const char* what,int a=0) {
    const char* const path="/fs/ram3/nbt-aa-plugin.log";
    struct stat st;
    int flags=O_WRONLY|O_CREAT|O_APPEND;
    if(stat(path,&st)==0&&st.st_size>65536)flags=O_WRONLY|O_CREAT|O_TRUNC;
    int fd=open(path,flags,0644);
    if(fd<0)return;
    struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t);
    char line[96]; int n=snprintf(line,sizeof(line),"%ld.%03ld %s %d\n",(long)t.tv_sec,t.tv_nsec/1000000,what,a);
    if(n>0)write(fd,line,(size_t)n);
    close(fd);
}
// reads native_service's public status; returns the text to show, *running = Android Auto session active
unsigned readStatus(bool* running) {
    *running=false;
    struct stat st;
    if(stat(kState,&st)!=0||time(0)-st.st_mtime>8) return kTxtServiceDown;   // rewritten every 2 s while alive
    char buf[160]; int fd=open(kState,O_RDONLY); if(fd<0) return kTxtServiceDown;
    ssize_t n=read(fd,buf,sizeof(buf)-1); close(fd); if(n<=0) return kTxtServiceDown; buf[n]=0;
    const bool phone=strstr(buf,"phone=1")!=0;
    if(strstr(buf,"state=running")){*running=true;return kTxtRunning;}
    if(strstr(buf,"state=connecting")||strstr(buf,"state=starting_decoder")||strstr(buf,"state=draining")) return kTxtStarting;
    if(strstr(buf,"state=replug_required")) return kTxtReplug;
    if(strstr(buf,"state=unsupported_device")) return kTxtUnsupported;
    if(strstr(buf,"state=several_devices")) return kTxtSeveral;
    if(strstr(buf,"state=waiting_for_hmi")) return phone?kTxtStarting:kTxtNoPhone;   // request just written
    if(strstr(buf,"state=waiting_for_phone")) return kTxtNoPhone;
    return kTxtConnectUsb;
}
class Entry;
// --- hold states (car 2026-11-07, second design) ---------------------------------------------------------------------
// Problem: every tilt LEFT acted as BACK and every tilt RIGHT opened the split screen; each was undone with a focus event
// (triggerEvent 52). After a few dozen of them the HMI stopped honouring the event (picture gone until a reboot).
// Now: kHoldCount nested inert states (13 .. 13+N-1, each the parent of the next, focus event 52 targets the deepest).
// BACK / tilt left only climbs one level and stays on a hold state: no event. Only when the entry page (10) is reached
// one event goes out. Side panels / popups: at most one re-hold per 1.5 s. Never more than 10 events per minute, and no
// periodic re-hold any more. The split screen itself is best switched off in iDrive (OPTION -> split screen).
const unsigned kHoldCount=8;
// Decides, shortly after a hold state lost focus, what happened:
//   another hold state has the focus (BACK climbed a level)         -> nothing
//   a hard key was pressed (flag from nbt_knob)                      -> hide
//   a hold state is still visible (side panel / popup)              -> one re-hold (rate-limited), else wait
//   entry page focused (all hold levels used up)                    -> one re-hold
//   else (MENU, MEDIA, ... left the page)                           -> hide
class Checker : public bmw::Runnable {
public:
    Entry* e; bool pending;
    Checker():e(0),pending(false) {}
    void run();
};
// Safety net: 2.5 s after a re-hold attempt a hold state must have the focus; otherwise the picture is hidden, so turns
// and presses never drive an invisible iDrive page underneath.
class Verifier : public bmw::Runnable {
public:
    Entry* e; bool pending;
    Verifier():e(0),pending(false) {}
    void run();
};
struct HoldSlot { Entry* e; unsigned idx; };
// Background check, once a second for the rest of the HMI's life (started at the first entry, because posts made from
// create() did not run on this HMI): when nbt_hwvideo reports that the video source which pushed Android Auto off the
// screen (reverse camera) is gone, Android Auto is brought back - but only if it was in front and the driver had not
// left it with a hard key (MENU, MEDIA, NAV, ...).
class Background : public bmw::Runnable {
public:
    Entry* e; bool started;
    Background():e(0),started(false) {}
    void run();
};
// Car data (ported from valinet/nbt-android-auto, car-tested on an original NBT, F30, 2026-11-17):
//  sensors.PDCStatus '{"PDCStatus":1}' = parking assistant / reverse camera view on screen, 2 = off. When it comes up
//  while the Android Auto picture is on screen, its end brings Android Auto back (the same resume path as nbt_hwvideo's
//  camera detection: kResume, acted on by Background). This replaces guessing from the video source.
//  controls.headlights '{"headlights":N}' -> /fs/ram3/nbt-aa-headlights: the session sends headlights on = night mode
//  to the phone (this NBT has no day/night car data property).
void cdsText(const bmw::BasicString<unsigned short>& v,char* out,size_t size) {
    const unsigned short* p=*reinterpret_cast<const unsigned short* const*>(&v);   // data_ = the UTF-16 text itself
    size_t n=0;
    while(p&&p[n]&&n<size-1){out[n]=p[n]<0x80?(char)p[n]:'?';++n;}
    out[n]=0;
}
void cdsFile(const char* name,const char* file,const char* text) {
    char line[200];snprintf(line,sizeof(line),"CDS %s = '%s'",name,text);
    plog(line,0);
    int fd=open(file,O_WRONLY|O_CREAT|O_TRUNC,0644);
    if(fd>=0){size_t l=strlen(text);if(write(fd,text,l)<0||write(fd,"\n",1)<0){}close(fd);}
}
bool g_pdcFromAa=false;   // the parking assistant came up over the Android Auto picture
bool onPdc(void*,const bmw::BasicString<unsigned short>& v){
    char text[160];cdsText(v,text,sizeof(text));
    cdsFile("sensors.PDCStatus","/fs/ram3/nbt-aa-pdc",text);
    const char* d=text;while(*d&&(*d<'0'||*d>'9'))++d;
    int st=*d?0:-1;   // no atoi: the plugin imports only what the release plugin imports
    while(*d>='0'&&*d<='9'){st=st*10+(*d-'0');++d;}
    static int lastSt=-1;   // the same value again (e.g. a repeat after the page was hidden) changes nothing
    if(st==lastSt)return true;
    lastSt=st;
    // the PDC value comes ~150 ms before the page loses focus, so "picture on screen" = no nbt-aa-hidden yet
    if(st==1){g_pdcFromAa=access(kHidden,F_OK)!=0;plog("parking assistant on, Android Auto was on screen",g_pdcFromAa?1:0);}
    else if(st==2&&g_pdcFromAa){g_pdcFromAa=false;touch(kResume);plog("parking assistant off: back to Android Auto",0);}
    return true;
}
bool onHeadlights(void*,const bmw::BasicString<unsigned short>& v){
    char text[160];cdsText(v,text,sizeof(text));
    cdsFile("controls.headlights","/fs/ram3/nbt-aa-headlights",text);
    return true;
}
bool g_cdsDone=false;
void cdsSubscribe(bmw::PluginApp* app) {
    if(g_cdsDone||!app)return;
    g_cdsDone=true;
    bmw::BasicString<char> p("sensors.PDCStatus"),h("controls.headlights");
    bmw::CdsDelegate dp={app,&onPdc},dh={app,&onHeadlights};
    app->cdsAddPropertyChangedEventHandler(p,0,dp);
    app->cdsAddPropertyChangedEventHandler(h,0,dh);
    plog("CDS handlers for sensors.PDCStatus and controls.headlights registered",0);
}
// sends focus event 53 (back to page 10) after a mode choice
class BackToEntry : public bmw::Runnable {
public:
    bmw::PluginApp* app;
    BackToEntry():app(0) {}
    // 53 first, then 700..709 (each focus event works only 11 times per HMI start, see kEvPoolFirst)
    void run() {
        if(!app)return;
        static unsigned uses=0;
        const unsigned ev=uses<10?kEvEntry:700+((uses-10)/10)%10;
        ++uses;
        plog("mode chosen -> back to page 10, focus event",(int)ev);app->triggerEvent(ev);
    }
};
class Entry : public bmw::Runnable {
public:
    bmw::PluginApp* app;
    bool stopping, polling, entryFocused, running;
    bool wantFront;   // Android Auto was in front and the driver did not leave it with a hard key (resume after camera)
    unsigned focusMask, visibleMask;
    unsigned shownText; int shownMode; long window; unsigned holds, ignoredHolds; long lastSideHold, lastAutoHold; long long lastSideHoldMs, lastHoldMs, lastFocusMs;
    unsigned evIdx, evUses, evIgnoredSeen;   // focus event pool: current event, its uses, ignoredHolds seen
    long lastGuardReset;                     // last loop guard reset by a driver entry (s)
    Checker checker;
    Verifier verifier;
    BackToEntry back;
    Background background;
    HoldSlot slots[kHoldCount];
    Entry():app(0),stopping(false),polling(false),entryFocused(false),running(false),wantFront(false),focusMask(0),visibleMask(0),
            shownText(0),shownMode(0),window(0),holds(0),ignoredHolds(0),lastSideHold(0),lastAutoHold(0),lastSideHoldMs(-100000),lastHoldMs(-100000),lastFocusMs(0),
            evIdx(kEvPoolCount-1),evUses(kEvPoolUses),evIgnoredSeen(0),lastGuardReset(-1000) {
        checker.e=this; verifier.e=this; background.e=this;
        for(unsigned i=0;i<kHoldCount;++i){slots[i].e=this;slots[i].idx=i;}
    }
    bool holdFocused() const { return focusMask!=0; }
    bool holdVisible() const { return visibleMask!=0; }
    void check(unsigned delay) { if(!checker.pending&&!stopping&&app){checker.pending=true;app->post(&checker,delay);} }
    // 5 s (car 2026-11-08: with the focus in the split screen iDrive gave it back only after ~4.4 s; 2.5 s hid the picture
    // in between)
    void verify() { if(!verifier.pending&&!stopping&&app){verifier.pending=true;app->post(&verifier,5000);} }
    void hold(const char* why) {
        // review 2026-11-09: on every entry with Android Auto running the checker (+30 ms) and the status poll (+50 ms)
        // each sent the event, 50 ms apart and in the middle of iDrive's page transition; in the log of 2026-11-09 the
        // first ignored event was exactly such a pair. One event per second at most.
        const long long tm=now_ms();
        if(tm-lastHoldMs<1000){plog("hold skipped (one was just sent)");return;}
        lastHoldMs=tm;lastAutoHold=now_s();
        long t=now_s();
        if(t-window>=60){window=t;holds=0;}
        if(++holds>10){plog("hold refused by loop guard (10/min)");touch(kHidden);return;}
        // focus event pool (see kEvPoolFirst): an ignored hold moves to the next, fresh event at once
        if(ignoredHolds!=evIgnoredSeen){evIgnoredSeen=ignoredHolds;evUses=kEvPoolUses;}
        if(evUses>=kEvPoolUses){evIdx=(evIdx+1)%kEvPoolCount;evUses=0;}
        ++evUses;
        const unsigned ev=kEvPoolFirst+evIdx;
        plog(why,(int)holds);
        plog("  focus event",(int)ev);
        plog("  ms since the last focus change",(int)(tm-lastFocusMs>100000?100000:tm-lastFocusMs));
        app->triggerEvent(ev);
        verify();
    }
    // HMI thread, every 500 ms while our page has focus: status text; a slow retry (every 10 s) when the entry page shows
    // while Android Auto runs and the last hold attempt did not take
    void run() {
        if(stopping||!app){polling=false;return;}
        if(!entryFocused&&!holdFocused()){polling=false;return;}
        unsigned text=readStatus(&running);
        if(text!=shownText){bmw::TextID tx(text);app->updateModel(kStatusModel,tx);shownText=text;}
        int mode=modeIs169()?1:2;
        if(mode!=shownMode){
            bmw::TextID a(mode==1?kTxt169On:kTxt169),b(mode==2?kTxtWideOn:kTxtWide);
            app->updateModel(kModel169,a);app->updateModel(kModelWide,b);shownMode=mode;
        }
        long t=now_s();
        if(running&&entryFocused&&!holdFocused()&&t-lastAutoHold>=10){lastAutoHold=t;hold("hold (entry page, retry)");}
        app->post(this,500);
    }
    void startPolling(unsigned delay) { if(!polling&&!stopping){polling=true;app->post(this,delay);} }
    static bool entryFocus(void* self,const bmw::Parcel& p) {
        Entry* e=static_cast<Entry*>(self);
        bool focused=false;p.readBool(focused);
        e->entryFocused=focused;e->lastFocusMs=now_ms();
        plog("entry focus",focused);
        if(focused&&!e->stopping){
            unlink(kLeave);                    // the driver came back on purpose
            touch(kRequest);
            // User report 2026-11-19: "shows connected but does not open when clicking Android Auto". After popups /
            // side panels the 10-per-minute loop guard was used up and refused every hold, also the one the driver asked
            // for by opening the page. The guard is for automatic loops: an entry that our own hold event did not cause
            // (none in the last 3 s) starts a fresh minute.
            // at most once a minute, so a slow automatic loop (entry page every few seconds) is still stopped
            if(now_ms()-e->lastHoldMs>3000&&e->holds&&now_s()-e->lastGuardReset>=60){
                plog("entry by the driver: loop guard reset",(int)e->holds);e->holds=0;e->window=now_s();e->lastGuardReset=now_s();}
            if(!e->background.started&&e->app){e->background.started=true;e->app->post(&e->background,1000);}
            cdsSubscribe(e->app);   // once: parking assistant return, night mode
            if(readStatus(&e->running)==kTxtServiceDown)launchService();   // service not running: start it now
            e->startPolling(50);if(e->running)e->check(30);
        }
        return true;
    }
    // mode pages 30 / 31: store the mode, restart the service, back to page 10
    static bool mode169Focus(void* self,const bmw::Parcel& p) { return modeFocus(static_cast<Entry*>(self),p,true); }
    static bool modeWideFocus(void* self,const bmw::Parcel& p) { return modeFocus(static_cast<Entry*>(self),p,false); }
    static bool modeFocus(Entry* e,const bmw::Parcel& p,bool split) {
        bool focused=false;p.readBool(focused);
        plog(split?"mode page 16:9 focus":"mode page widescreen focus",focused);
        if(focused&&!e->stopping){setMode(split);e->shownMode=0;e->app->post(&e->back,400);}
        return true;
    }
    static bool restartFocus(void* self,const bmw::Parcel& p) {
        Entry* e=static_cast<Entry*>(self);
        bool focused=false;p.readBool(focused);
        plog("restart page focus",focused);
        if(focused&&!e->stopping){
            restartService(0);
            e->holds=0;e->window=now_s();e->lastSideHoldMs=-100000;   // fresh loop guard for the new session
            e->app->post(&e->back,400);
        }
        return true;
    }
    static bool holdFocus(void* self,const bmw::Parcel& p) {
        HoldSlot* s=static_cast<HoldSlot*>(self); Entry* e=s->e;
        bool focused=false;p.readBool(focused);
        if(focused)e->focusMask|=1u<<s->idx; else e->focusMask&=~(1u<<s->idx);
        e->lastFocusMs=now_ms();
        plog(focused?"hold focus 1, level":"hold focus 0, level",(int)s->idx);
        if(focused){unlink(kLeave);unlink(kHidden);unlink(kResume);e->ignoredHolds=0;e->wantFront=true;e->startPolling(500);}
        else e->check(150);            // decide after the HMI has finished its transition (see Checker)
        return true;
    }
    static bool holdVisibility(void* self,const bmw::Parcel& p) {
        HoldSlot* s=static_cast<HoldSlot*>(self); Entry* e=s->e;
        bool visible=false;p.readBool(visible);
        if(visible)e->visibleMask|=1u<<s->idx; else e->visibleMask&=~(1u<<s->idx);
        return true;
    }
};
void Checker::run() {
    pending=false;
    if(!e||e->stopping||!e->app||e->holdFocused())return;    // BACK only climbed one hold level: nothing to do
    readStatus(&e->running);
    if(unlink(kLeave)==0){plog("check: leave key");e->wantFront=false;touch(kHidden);return;}
    if(e->running&&e->holdVisible()){
        // tilt right puts the iDrive focus into the split screen: taken back at once, so a normal tilt right is not noticed
        // in Android Auto; a second tilt within 1.5 s still reaches the split screen menu (user request 2026-11-08: not
        // aggressive, the HMI stopped honouring a flood of these events before)
        long long tm=now_ms();
        if(tm-e->lastSideHoldMs>=1500){e->lastSideHoldMs=tm;e->hold("check: side panel/popup -> hold");}
        else plog("check: side panel/popup, waiting");
        return;
    }
    if(e->running&&e->entryFocused){e->hold("check: entry page reached -> hold");return;}
    plog("check: page left");
    touch(kHidden);                                        // the page was left: hide the picture
}
void Background::run() {
    if(!e||e->stopping||!e->app){started=false;return;}
    if(access(kResume,F_OK)==0&&now_ms()-e->lastHoldMs>=1000){   // a hold just went out: keep it for the next tick
        unlink(kResume);
        readStatus(&e->running);
        if(e->wantFront&&e->running&&!e->holdFocused()&&access(kLeave,F_OK)!=0){
            // a fresh guard: this is one event after a reverse manoeuvre, not a loop
            e->holds=0;e->window=now_s();
            e->hold("resume: camera gone -> hold");
        } else plog("resume ignored (not in front before / left by key / not running)",(int)e->wantFront);
    }
    readStatus(&e->running);
    if(!e->running)e->wantFront=false;     // no session: nothing to resume
    e->app->post(this,1000);
}
void Verifier::run() {
    pending=false;
    if(!e||e->stopping||!e->app||e->holdFocused())return;
    plog("verify: no hold state focused -> hide");
    ++e->ignoredHolds;
    touch(kHidden);
}
Entry entry;
bool created=false;
bmw::IPluginApp* create(bmw::IDispatcher& d,const char* name,const bmw::Bundle& p,const bmw::Bundle& c) {
    if(created)return 0;
    bmw::PluginApp* app=new bmw::PluginApp(d,bmw::BasicString<char>(name),p,c);
    entry.app=app;entry.stopping=false;entry.polling=false;entry.checker.e=&entry;entry.checker.pending=false;entry.verifier.e=&entry;entry.verifier.pending=false;
    entry.focusMask=entry.visibleMask=0;
    entry.wantFront=false;entry.background.e=&entry;entry.background.started=false;
    bmw::ActionDelegate onEntry={&entry,&Entry::entryFocus};
    app->addHandler(kEntryState,kFocus,onEntry);
    entry.back.app=app;entry.shownMode=0;
    bmw::ActionDelegate on169={&entry,&Entry::mode169Focus};
    bmw::ActionDelegate onWide={&entry,&Entry::modeWideFocus};
    app->addHandler(kMode169State,kFocus,on169);
    app->addHandler(kModeWideState,kFocus,onWide);
    bmw::ActionDelegate onRestart={&entry,&Entry::restartFocus};
    app->addHandler(kRestartState,kFocus,onRestart);
    for(unsigned i=0;i<kHoldCount;++i){
        bmw::ActionDelegate onHold={&entry.slots[i],&Entry::holdFocus};
        bmw::ActionDelegate onHoldVisible={&entry.slots[i],&Entry::holdVisibility};
        app->addHandler(kHoldState+i,kFocus,onHold);
        app->addHandler(kHoldState+i,kVisible,onHoldVisible);  // type 11 = VISIBLE, as BMW's ActiveSeating registers it
    }
    created=true;
    touch(kHidden);   // nothing is shown before the page is opened
    {
        pthread_attr_t at; pthread_t th;
        pthread_attr_init(&at); pthread_attr_setdetachstate(&at,PTHREAD_CREATE_DETACHED);
        pthread_create(&th,&at,autostartThread,0);
        pthread_attr_destroy(&at);
    }
    return reinterpret_cast<bmw::IPluginApp*>(app);
}
void destroy(bmw::IPluginApp* p) {
    if(!created)return;
    g_cdsDone=false;   // a new create() gets a new app: subscribe again
    entry.stopping=true;
    touch(kHidden);
    entry.checker.e=0;entry.verifier.e=0;entry.back.app=0;entry.background.e=0;
    delete reinterpret_cast<bmw::PluginApp*>(p);   // native handler drops the queued runnable (entry is static)
    entry.app=0;
    created=false;
}
}
struct PluginAppInfo {
    uint16_t major,minor;const char* name;const char* vendor;
    bmw::IPluginApp*(*create)(bmw::IDispatcher&,const char*,const bmw::Bundle&,const bmw::Bundle&);
    void(*destroy)(bmw::IPluginApp*);
};
typedef char InfoSize[(sizeof(PluginAppInfo)==20)?1:-1];
extern "C" { PluginAppInfo pluginAppInfo={1,0,"NbtAndroidAuto","NBT-Project",create,destroy}; }

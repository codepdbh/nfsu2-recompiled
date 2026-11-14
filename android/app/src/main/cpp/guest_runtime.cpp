#include "game_log.h"
#include "host_performance.h"
#include <initializer_list>
#include <set>
#include <condition_variable>
#include "guest_runtime.h"
#include "android_resolution.h"
#include "guest_memory.h"
#include "guest_heap.h"
#include "runtime_log.h"
#include "frame_pacer.h"
#if defined(__ANDROID__) || defined(NFS_D3D9_BACKEND)
#include "d3d9_bridge.h"
#include "nfs_widescreen.h"
#endif
#include <array>
#include <vector>
#include <map>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <fstream>
#include <iterator>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cstring>
#include <cctype>
#include <chrono>
#include <ctime>
#include <thread>
#include <atomic>
#include <unistd.h>
#include <pthread.h>
#include <sched.h>
#include <filesystem>
#include <fcntl.h>
#include <sys/stat.h>
#include <cerrno>
#include <deque>
#ifdef __ANDROID__
#include <aaudio/AAudio.h>
#endif
extern "C" {
#include "recomp_types.h"
#include "pe_format.h"
#include "nfs_runtime.h"

uint32_t g_eax, g_ecx, g_edx, g_esp, g_ebx, g_esi, g_edi, g_ebp;
#ifndef RECOMP_X87_WINDOW
double g_st[8];
#endif
int g_fp_top;
uint16_t g_fpu_cw = 0x027f;
uint16_t g_seg_cs, g_seg_ds, g_seg_es, g_seg_fs, g_seg_gs, g_seg_ss;
uint64_t g_mm[8];
V128 g_xmm[8];
uint32_t g_mxcsr = 0x1f80;
uint32_t g_fs_base, g_gs_base, g_cur_func;
ptrdiff_t g_mem_base;
uint32_t g_icall_trace[ICALL_TRACE_SIZE], g_icall_from[ICALL_TRACE_SIZE];
uint32_t g_icall_trace_idx, g_icall_count;
}

namespace {
std::atomic<uint32_t> displayWidth{1280},displayHeight{720};
std::atomic<uint32_t> renderWidth{0},renderHeight{0};
std::atomic<unsigned> frameLimit{0};
std::string hex(uint32_t va) {
    std::ostringstream out; out << "0x" << std::hex << std::setw(8) << std::setfill('0') << va;
    return out.str();
}
struct GuestStop : std::runtime_error { using std::runtime_error::runtime_error; };
struct State {
    uint32_t eax{}, ecx{}, edx{}, esp{}, ebx{}, esi{}, edi{}, ebp{}, fs{}, gs{}, cur{};
    uint32_t flags[4]{}, mxcsr{0x1f80};
    uint16_t segments[6]{}, cw{0x027f};
    double st[8]{}; int top{};
    uint64_t mm[8]{}; V128 xmm[8]{};
    void save() {
        eax=g_eax; ecx=g_ecx; edx=g_edx; esp=g_esp; ebx=g_ebx; esi=g_esi; edi=g_edi; ebp=g_ebp;
        fs=g_fs_base; gs=g_gs_base; cur=g_cur_func; mxcsr=g_mxcsr;
        flags[0]=g_flag_k; flags[1]=g_flag_a; flags[2]=g_flag_b; flags[3]=g_flag_cf;
        segments[0]=g_seg_cs; segments[1]=g_seg_ds; segments[2]=g_seg_es;
        segments[3]=g_seg_fs; segments[4]=g_seg_gs; segments[5]=g_seg_ss;
        std::memcpy(st,g_st,sizeof st); top=g_fp_top; cw=g_fpu_cw;
        std::memcpy(mm,g_mm,sizeof mm); std::memcpy(xmm,g_xmm,sizeof xmm);
    }
    void load() const {
        g_eax=eax; g_ecx=ecx; g_edx=edx; g_esp=esp; g_ebx=ebx; g_esi=esi; g_edi=edi; g_ebp=ebp;
        g_fs_base=fs; g_gs_base=gs; g_cur_func=cur; g_mxcsr=mxcsr;
        g_flag_k=flags[0]; g_flag_a=flags[1]; g_flag_b=flags[2]; g_flag_cf=flags[3];
        g_seg_cs=segments[0]; g_seg_ds=segments[1]; g_seg_es=segments[2];
        g_seg_fs=segments[3]; g_seg_gs=segments[4]; g_seg_ss=segments[5];
        std::memcpy(g_st,st,sizeof st); g_fp_top=top; g_fpu_cw=cw;
        std::memcpy(g_mm,mm,sizeof mm); std::memcpy(g_xmm,xmm,sizeof xmm);
    }
};
struct Import { std::string name; recomp_func_t function{}; uint64_t calls{}; };
std::unique_ptr<GuestMemory> memory;
std::unique_ptr<GuestHeaps> heaps;
// The guest machine lock: FIFO by ticket and recursive per thread, with the
// std::recursive_mutex interface the scheduler code already uses.
//
// std::recursive_mutex is not fair. The game thread releases the machine
// around imports and every 500 us, then asks for it again at once and usually
// wins; the guest audio mixer thread waited behind it, filled its DirectSound
// ring too late, and the mixer replayed stale data ("the sound loops"). The
// same starvation made the Windows build stutter until native32 got a ticket
// lock. Here every unlock hands the machine to the longest waiter.
// Per guest thread: how long it held the machine, how often it took it and how
// long it queued for it. Reported per frame in the Present statistics, so the
// scheduler's cost is visible per thread (the render thread's "waiting" alone
// does not say who had the machine meanwhile). Slot 0 is the main/render
// thread; guest threads claim a slot in guestThreadEntry.
struct SchedStats{std::atomic<uint64_t> holdNs{0},acquisitions{0},waitNs{0};std::atomic<uint32_t> guestId{0};};
constexpr unsigned schedSlotCount=16;
SchedStats schedStats[schedSlotCount];
thread_local unsigned schedSlot=0;
std::atomic<unsigned> schedSlotsClaimed{1};
unsigned claimSchedSlot(uint32_t guestId){
    unsigned slot=std::min(schedSlotsClaimed.fetch_add(1),schedSlotCount-1);
    schedStats[slot].guestId.store(guestId,std::memory_order_relaxed);return slot;
}
// The thread that presents frames: served ahead of the FIFO when it queues
// for the machine, within the bounds below.
thread_local bool machinePriority=false;
// The machine lock. Fair by default: unlock() hands the machine straight to
// the longest-queued thread (no ticket window a newcomer could barge into),
// and wakes only that thread -- a single shared condition variable woke every
// waiter per handover, and the audio mixer thread starving behind others made
// "the sound loop" on the Redmi Note 8. The render thread may jump the queue,
// bounded: never past a thread already queued for 2 ms (less than half an
// AAudio burst, so the mixer always gets its turn within a burst) and never
// more than 3 times in a row. On a Dimensity 8400 free-roam run the render
// thread spent 10-12 ms of a 30 ms frame queued behind threads that then held
// the machine for 0.03-0.08 ms each.
class MachineMutex {
    struct Waiter{std::thread::id id;std::condition_variable cv;std::chrono::steady_clock::time_point since;bool priority;};
    std::mutex m_;
    std::vector<Waiter*> waiters_;   // FIFO; a waiter lives on its thread's stack until served
    std::atomic<std::thread::id> owner_{};   // atomic: queued threads spin on it before sleeping
    unsigned depth_=0,priorityJumps_=0;
    static constexpr std::chrono::milliseconds maxQueueBeforeFair{2};
    static constexpr unsigned maxPriorityJumps=3;
    // A queued thread spins this long before sleeping on its condition
    // variable: a futex wake costs ~0.3 ms here and the machine changes hands
    // ~13 times per frame, which was most of the render thread's 9 ms/frame
    // of waiting. The Dimensity 8400 has eight performance cores and the game
    // keeps two busy, so the spinning core is free anyway.
#if defined(NFS_GUEST_FAST)
    static constexpr std::chrono::microseconds spinBeforeSleep{1500},prioritySpinBeforeSleep{4000};
#else
    static constexpr std::chrono::microseconds spinBeforeSleep{500},prioritySpinBeforeSleep{2000};   // portable build: less heat
#endif
    void took(std::chrono::steady_clock::time_point queued){
        acquired=std::chrono::steady_clock::now();
        auto& stats=schedStats[schedSlot];
        stats.waitNs.fetch_add(uint64_t((acquired-queued).count()),std::memory_order_relaxed);
        stats.acquisitions.fetch_add(1,std::memory_order_relaxed);
    }
public:
    void lock(){
        const auto me=std::this_thread::get_id();
        std::unique_lock<std::mutex> l(m_);
        if(owner_.load(std::memory_order_relaxed)==me){++depth_;return;}
        auto queued=std::chrono::steady_clock::now();
        if(owner_.load(std::memory_order_relaxed)==std::thread::id{}&&waiters_.empty()){owner_=me;depth_=1;took(queued);return;}
        Waiter self{me,{},queued,machinePriority};
        waiters_.push_back(&self);
        l.unlock();
        auto spinUntil=queued+(machinePriority?prioritySpinBeforeSleep:spinBeforeSleep);
        // sched_yield per iteration (a bare yield instruction kept the core from
        // the thread being waited for and the clock read became 23% of the
        // render thread's samples); the clock is read every 8 iterations.
        for(unsigned spins=0;owner_.load(std::memory_order_acquire)!=me;){
            if((++spins&7u)==0&&std::chrono::steady_clock::now()>=spinUntil)break;
            sched_yield();
        }
        l.lock();
        self.cv.wait(l,[&]{return owner_.load(std::memory_order_relaxed)==me;});   // unlock() removed us from the queue and handed over
        depth_=1;took(queued);
    }
    // When the current owner got the machine; only the owner reads it.
    std::chrono::steady_clock::time_point acquired{};
    bool try_lock(){
        const auto me=std::this_thread::get_id();
        std::lock_guard<std::mutex> l(m_);
        if(owner_.load(std::memory_order_relaxed)==me){++depth_;return true;}
        if(owner_.load(std::memory_order_relaxed)!=std::thread::id{}||!waiters_.empty())return false;
        owner_=me;depth_=1;took(std::chrono::steady_clock::now());return true;
    }
    bool hasWaiters(){
        std::lock_guard<std::mutex> lock(m_);
        return !waiters_.empty();
    }
    void unlock(){
        std::lock_guard<std::mutex> l(m_);
        if(--depth_)return;
        schedStats[schedSlot].holdNs.fetch_add(uint64_t((std::chrono::steady_clock::now()-acquired).count()),std::memory_order_relaxed);
        if(waiters_.empty()){owner_.store(std::thread::id{},std::memory_order_release);return;}
        size_t pick=0;
        for(size_t i=1;i<waiters_.size();++i)if(waiters_[i]->priority){pick=i;break;}
        if(pick){
            bool fair=std::chrono::steady_clock::now()-waiters_[0]->since>=maxQueueBeforeFair||priorityJumps_>=maxPriorityJumps;
            if(fair)pick=0;else ++priorityJumps_;
        }
        if(!pick)priorityJumps_=0;
        Waiter* next=waiters_[pick];waiters_.erase(waiters_.begin()+ptrdiff_t(pick));
        owner_.store(next->id,std::memory_order_release);
        // Notified under m_: a spurious wakeup could otherwise see owner_==me,
        // return and destroy the Waiter before notify_one touched it.
        next->cv.notify_one();
    }
};
MachineMutex machine;
thread_local std::unique_lock<MachineMutex>* machineLease=nullptr;
thread_local uint32_t guestThreadId=1;
std::vector<Import> imports;
std::map<std::string,uint32_t> modules;
thread_local uint32_t lastError=0;
uint32_t imageBase{}, imageSize{}, entry{};
uint32_t resourceRva{},resourceSize{};
// Set and read under the machine lock with no yield in between (lookup -> call):
// plain globals, not thread_local, which cost a TLS descriptor call per COM call.
uint32_t lastImport{};
std::string hostGameRoot;
std::string guestLanguage="Spanish";
bool guestWidescreen=true;
std::atomic<bool> stopRequested{false};
std::atomic<bool> guestPaused{false};
std::chrono::steady_clock::time_point bootDeadline;
std::string guestThreadFault;
bool timerTestActive=false;std::vector<uint32_t> timerTestCalls;
constexpr uint32_t timerTestAddress=0x7b000010u;
bool generatedTestActive=false;unsigned generatedTestCalls=0;
constexpr uint32_t generatedTestTarget=0x7b000020u;
uint32_t read32(uint32_t va);
// Stall diagnostics: where each guest thread last handed the machine over and
// what it was waiting for, dumped when the game stops presenting frames.
struct GuestThreadTrace {
    std::atomic<uint32_t> func{0},handle{0},esp{0};std::atomic<const char*> wait{nullptr};
    std::array<std::atomic<uint32_t>,8> held{};  // critical sections this thread owns (outermost entries)
    void hold(uint32_t cs){for(auto& slot:held){uint32_t empty=0;if(slot.compare_exchange_strong(empty,cs))return;}}
    void release(uint32_t cs){for(auto& slot:held){uint32_t value=cs;if(slot.compare_exchange_strong(value,0))return;}}
};
std::mutex threadTracesMutex;
std::map<uint32_t,std::shared_ptr<GuestThreadTrace>> threadTraces;
thread_local std::shared_ptr<GuestThreadTrace> threadTrace;
GuestThreadTrace& currentTrace(){
    if(!threadTrace){threadTrace=std::make_shared<GuestThreadTrace>();std::lock_guard<std::mutex> lock(threadTracesMutex);threadTraces[guestThreadId]=threadTrace;}
    return *threadTrace;
}
void forgetThreadTrace(){std::lock_guard<std::mutex> lock(threadTracesMutex);threadTraces.erase(guestThreadId);threadTrace.reset();}
struct WaitScope {
    WaitScope(const char* what,uint32_t handle){auto& trace=currentTrace();trace.wait=what;trace.handle=handle;}
    ~WaitScope(){currentTrace().wait=nullptr;}
};
std::atomic<int64_t> lastPresentMs{0};
int64_t steadyMs(){return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
void startStallWatch(){
    static std::once_flag started;
    std::call_once(started,[]{std::thread([]{
        int64_t reportedFor=0;
        while(!stopRequested.load()){
            std::this_thread::sleep_for(std::chrono::seconds(2));
            int64_t last=lastPresentMs.load(),idle=steadyMs()-last;
            if(guestPaused.load()||idle<12000||reportedFor==last)continue;
            reportedFor=last;
            NFS_RUNTIME_LOG(ANDROID_LOG_WARN,"NFSU2","Guest stall: no frame for %lld s; guest threads:",static_cast<long long>(idle/1000));
            std::lock_guard<std::mutex> lock(threadTracesMutex);
            for(auto& [id,trace]:threadTraces){
                const char* wait=trace->wait.load();uint32_t handle=trace->handle.load();
                std::string detail,held,callers;
                // Read-only peeks at guest memory from this host thread: stable while the guest is stuck.
                if(wait&&!std::strcmp(wait,"EnterCriticalSection")&&memory&&memory->readable(handle,24))
                    detail=" (owner thread "+std::to_string(read32(handle+12))+", count "+std::to_string(read32(handle+8))+")";
                for(auto& slot:trace->held)if(uint32_t cs=slot.load())held+=" "+hex(cs);
                uint32_t esp=trace->esp.load();
                for(unsigned i=0,found=0;memory&&i<256&&found<10;++i){
                    if(!memory->readable(esp+i*4,4))break;
                    uint32_t value=read32(esp+i*4);
                    if(value>=0x401000u&&value<0x75d000u&&recomp_lookup(value)==nullptr){callers+=" "+hex(value);++found;}
                }
                NFS_RUNTIME_LOG(ANDROID_LOG_WARN,"NFSU2","  thread %u at %08x %s %08x%s; holds:%s; stack:%s",id,trace->func.load(),
                    wait?wait:"running",wait?handle:0u,detail.c_str(),held.empty()?" none":held.c_str(),callers.c_str());
            }
        }
    }).detach();});
}
void yieldGuestMachine(unsigned milliseconds=0,std::chrono::steady_clock::time_point deadline={}) {
    if(!machineLease||!machineLease->owns_lock())throw GuestStop("Guest scheduler ownership missing");
    {auto& trace=currentTrace();trace.func=g_cur_func;trace.esp=g_esp;}
    State saved;saved.save();machineLease->unlock();
    if(deadline!=std::chrono::steady_clock::time_point{})std::this_thread::sleep_until(deadline);
    else if(milliseconds)std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));else std::this_thread::yield();
    machineLease->lock();saved.load();
}
thread_local bool renderThread=false;
// Render-thread slice handovers and the time spent getting the machine back.
uint64_t renderYields=0;std::chrono::nanoseconds renderWait{};
void checkGuestProgress() {
    while(guestPaused.load()&&!stopRequested.load())yieldGuestMachine(20);
    if(stopRequested.load())throw GuestStop("Guest execution cancelled");
    // Imports and lifted back edges can reach this hook many times per frame.
    // Keep cancellation immediate, but amortize full register saves and native
    // scheduler handovers over a short time slice rather than every call.
    // One time slice for whichever guest thread holds the machine.
    //
    // The render thread (the one that presents) gets 3 ms slices, the others
    // 500 us. Every handover wakes the queued threads one after another; on the
    // Redmi Note 8 profile the render thread spent 56% of its wall time waiting
    // to get the machine back after 500 us slices, while the other guest
    // threads used ~10% of a CPU. It also yields once after each Present.
    // Slices count from when this thread acquired the machine: a shared
    // deadline left the incoming thread an expired slice, so it handed the
    // machine straight back after a few checkpoints (ping-pong).
    static unsigned checkpoints=0;
    if((++checkpoints&63u)==0){
        auto now=std::chrono::steady_clock::now();
        if(now>bootDeadline)throw GuestStop("CRT bring-up time budget reached at "+hex(g_cur_func));
        // With spinning handovers (MachineMutex) a slice costs the render
        // thread only the other thread's hold time, so it yields almost as
        // soon as somebody queues: 3 ms slices left waiters asleep past their
        // spin budget and each handover cost a ~0.3 ms futex wake.
        auto slice=renderThread?std::chrono::microseconds(300):std::chrono::microseconds(500);
        if(now-machine.acquired>=slice&&machine.hasWaiters()){
            yieldGuestMachine();
            if(renderThread){++renderYields;renderWait+=std::chrono::steady_clock::now()-now;}
        }
    }
    if(!guestThreadFault.empty())throw GuestStop(guestThreadFault);
}
// Event-driven guest waits. Guest wait objects (events, mutexes, threads,
// timers, critical sections, window messages) change only under the machine
// lock or messageMutex. A blocked thread registers the objects it waits on,
// releases the machine and sleeps until one of them is signaled or its
// deadline passes, instead of re-queuing for the machine every millisecond:
// with 100 one-shot timers/s in a race the timer worker alone made ~1000
// machine acquisitions/s, each one a handover the render thread had to wait
// for. Signals are per object: a global broadcast woke every blocked thread
// several times per frame (SetEvent, LeaveCriticalSection, PostMessage), each
// taking the machine for 0.03 ms just to go back to sleep. Registration
// precedes the readiness check, so a signal from a host thread (window
// messages) between the check and the sleep is still seen. The 50 ms cap
// turns a forgotten signaler into the old polling rather than a hang.
struct SyncWaiter{const void* const* keys;size_t count;bool woken=false;std::condition_variable cv;};
std::mutex syncMutex;std::vector<SyncWaiter*> syncWaiters;
// key = the object that changed; nullptr wakes everyone (stop, pause).
void signalGuestSync(const void* key=nullptr){
    std::lock_guard<std::mutex> l(syncMutex);
    for(auto* waiter:syncWaiters){
        if(waiter->woken)continue;
        bool hit=!key;for(size_t i=0;!hit&&i<waiter->count;++i)hit=waiter->keys[i]==key;
        if(hit){waiter->woken=true;waiter->cv.notify_one();}   // under syncMutex: the waiter cannot leave before this
    }
}
constexpr auto syncSafetyCap=std::chrono::milliseconds(50);
constexpr auto noDeadline=std::chrono::steady_clock::time_point::max();
std::chrono::steady_clock::time_point guestDeadline(uint32_t milliseconds){
    return milliseconds==UINT32_MAX?noDeadline:std::chrono::steady_clock::now()+std::chrono::milliseconds(milliseconds);
}
// ready() runs under the machine lock; returns false on deadline.
template<class Ready> bool blockGuestUntil(Ready&& ready,std::chrono::steady_clock::time_point deadline,const void* const* keys,size_t count){
    for(;;){
        SyncWaiter self{keys,count};
        {std::lock_guard<std::mutex> l(syncMutex);syncWaiters.push_back(&self);}
        struct Unregister{SyncWaiter* self;~Unregister(){std::lock_guard<std::mutex> l(syncMutex);syncWaiters.erase(std::find(syncWaiters.begin(),syncWaiters.end(),self));}} unregister{&self};
        checkGuestProgress();
        if(ready())return true;
        auto now=std::chrono::steady_clock::now();
        if(now>=deadline)return false;
        if(!machineLease||!machineLease->owns_lock())throw GuestStop("Guest scheduler ownership missing");
        {auto& trace=currentTrace();trace.func=g_cur_func;trace.esp=g_esp;}
        State saved;saved.save();machineLease->unlock();
        {std::unique_lock<std::mutex> l(syncMutex);
         self.cv.wait_until(l,std::min(deadline,now+syncSafetyCap),[&]{return self.woken||stopRequested.load()||guestPaused.load();});}
        machineLease->lock();saved.load();
    }
}
template<class Ready> bool blockGuestUntil(Ready&& ready,std::chrono::steady_clock::time_point deadline,std::initializer_list<const void*> keys={}){
    return blockGuestUntil(ready,deadline,keys.begin(),keys.size());
}
constexpr uint32_t importBase = 0x7f000000u;
constexpr uint32_t stackBase = 0x20000000u, stackSize = 16u << 20, tib = 0x21000000u;

void* ptr(uint32_t va, uint64_t bytes) { return memory->address(va, bytes); }
uint32_t read32(uint32_t va) { uint32_t v; std::memcpy(&v, ptr(va,4),4); return v; }
void write32(uint32_t va, uint32_t v) { std::memcpy(ptr(va,4),&v,4); }
uint32_t arg(uint32_t index) { return read32(g_esp + 4 + index*4); }
void ret(uint32_t value, uint32_t argc) { g_eax=value; g_esp+=4+argc*4; }
std::string guestString(uint32_t address) {
    if(!address) return {};
    std::string s;
    for(uint32_t i=0;i<32768;++i) {
        char c=*static_cast<char*>(ptr(address+i,1));
        if(!c) return s;
        s.push_back(c);
    }
    throw GuestStop("Unterminated guest string at "+hex(address));
}
std::string lower(std::string s) {
    std::transform(s.begin(),s.end(),s.begin(),[](unsigned char c){return char(std::tolower(c));}); return s;
}
uint32_t allocate(uint32_t bytes) {
    uint32_t result=heaps->alloc(GuestHeaps::process,8,bytes);
    if(!result) throw GuestStop("Guest allocator exhausted");
    return result;
}
void unsupported() {
    uint32_t index = (lastImport-importBase)/16;
    throw GuestStop("HLE pendiente: " + imports.at(index).name + " desde " + hex(g_cur_func));
}
void getVersion() {
    uint32_t out = arg(0), bytes=read32(out);
    if (bytes != 148 && bytes != 156) { ret(0,1); return; }
    std::memset(ptr(out+4,bytes-4),0,bytes-4);
    write32(out+4,5); write32(out+8,1); write32(out+12,2600); write32(out+16,2);
    if (bytes==156) { auto* p=static_cast<unsigned char*>(ptr(out+148,8)); p[6]=1; }
    ret(1,1);
}
// guest_modules.inc (game DLLs run as lifted code) needs callGuest, so it comes later.
uint32_t loadGuestModule(const std::string& name);
bool freeGuestModule(uint32_t handle);
uint32_t guestModuleHandle(const std::string& name);
bool guestModuleExport(uint32_t handle,const std::string& name,uint32_t& va);
bool isGuestModule(uint32_t handle);
void moduleHandle() {
    if(!arg(0)) { ret(imageBase,1); return; }
    auto name=lower(guestString(arg(0)));
    auto separator=name.find_last_of("/\\"); if(separator!=std::string::npos) name=name.substr(separator+1);
    if(name=="speed2.exe") { ret(imageBase,1); return; }
    if(name.find('.')==std::string::npos) name+=".dll";
    if(uint32_t loaded=guestModuleHandle(name)) { ret(loaded,1); return; }
    auto module=modules.find(name);
    static std::set<std::string> loggedLookups;
    if(loggedLookups.insert(name).second)
        NFS_RUNTIME_LOG(ANDROID_LOG_INFO,"NFSU2","Module lookup %s",name.c_str());
    if(module==modules.end()) lastError=126;
    ret(module==modules.end()?0:module->second,1);
}
void heapCreate() {
    ret(heaps->create(arg(0),arg(1),arg(2)),3);
}
void heapAlloc() { ret(heaps->alloc(arg(0),arg(1),arg(2)),3); }
void heapFree() { ret(heaps->free(arg(0),arg(2)),3); }
void heapRealloc() { ret(heaps->realloc(arg(0),arg(1),arg(2),arg(3)),4); }
void heapSize() { ret(heaps->size(arg(0),arg(2)),3); }
void heapDestroy() { ret(heaps->destroy(arg(0)),1); }
void getProcessHeap() { ret(GuestHeaps::process,0); }
void getProcAddress();
#if defined(__ANDROID__) || defined(NFS_D3D9_BACKEND)
uint32_t d3dToken{};
void direct3DCreate(){ret(createGuestD3D9(arg(0)),1);}
void freeD3DGuest(uint32_t address){heaps->free(GuestHeaps::process,address);}
void serviceGuestSound();
void d3dDispatch(){
    // The arguments are read in place on the guest stack (one bounds check)
    // instead of copying 128 bytes for each of the ~40k calls of a race frame.
    const uint32_t* args=static_cast<const uint32_t*>(ptr(g_esp+4,128));uint32_t count=0;uint32_t token=d3dToken;
    static uint64_t bridgeCalls=0;static std::chrono::nanoseconds bridgeTime{},presentTime{};
    // Time one call in 64 (and every Present) so the statistic does not cost
    // two clock reads on each of the ~40k calls in a race frame.
    bool timed=(++bridgeCalls&63u)==0||token==0x7e002110u;
    std::chrono::steady_clock::time_point bridgeStart;if(timed)bridgeStart=std::chrono::steady_clock::now();uint32_t result;
    try{result=dispatchGuestD3D9(token,args,&count);}catch(...){
        NFS_RUNTIME_LOG(ANDROID_LOG_ERROR,"NFSU2","D3D9 guest caller=%08x stack=%08x token=%08x self=%08x",g_cur_func,g_esp,token,args[0]);throw;}
    // Per method (kind*256+method), to name the calls that block the frame.
    static std::array<std::chrono::nanoseconds,4096> methodTime{};
    if(timed){auto spent=std::chrono::steady_clock::now()-bridgeStart;bridgeTime+=token==0x7e002110u?spent:spent*64;presentTime+=token==0x7e002110u?spent:std::chrono::nanoseconds{};
        methodTime[((token-0x7e000000u)/16u)&4095u]+=token==0x7e002110u?spent:spent*64;}
    if(token==0x7e002110u){nfs_widescreen_apply_frame_rate();lastPresentMs=steadyMs();startStallWatch();}
    if(token==0x7e002110u&&result==0){
        static FramePacer pacer;
        auto now=std::chrono::steady_clock::now();
        // Work since the frame began, without the pacer's wait, for the system's frame hints.
        static FrameWorkHint frameHint;static std::chrono::steady_clock::time_point frameBegan=now;
        {unsigned limit=frameLimit.load(std::memory_order_relaxed);
         frameHint.reportFrame(std::chrono::duration_cast<std::chrono::nanoseconds>(now-frameBegan).count(),1000000000ll/(limit?limit:60));}
        auto deadline=pacer.deadline(now,frameLimit.load(std::memory_order_relaxed));
        // The renderer has returned; guest audio/streaming workers can use the
        // machine while this thread waits. Late frames incur no extra delay.
        renderThread=true;machinePriority=true;
        if(deadline>now)yieldGuestMachine(0,deadline);
        else if(machine.hasWaiters()){auto before=std::chrono::steady_clock::now();yieldGuestMachine();++renderYields;renderWait+=std::chrono::steady_clock::now()-before;}
        frameBegan=std::chrono::steady_clock::now();
        static unsigned frames=0;static auto since=std::chrono::steady_clock::now();
        if(++frames==120){serviceGuestSound();auto now=std::chrono::steady_clock::now();double seconds=std::chrono::duration<double>(now-since).count();
            NFS_RUNTIME_LOG(ANDROID_LOG_INFO,"NFSU2","Guest D3D9 presentation %.1f FPS over %.2f seconds; bridge ~%.2f ms/frame (present %.2f), %.0f calls/frame",
                frames/seconds,seconds,double(bridgeTime.count())/1e6/frames,double(presentTime.count())/1e6/frames,double(bridgeCalls)/frames);
            std::array<unsigned,5> top{};
            for(unsigned slot=0;slot<4096u;++slot)for(unsigned rank=0;rank<top.size();++rank)
                if(methodTime[slot]>methodTime[top[rank]]){std::copy_backward(top.begin()+rank,top.end()-1,top.end());top[rank]=slot;break;}
            char detail[256];int used=0;
            for(unsigned slot:top)if(methodTime[slot].count()>0&&used<200)
                used+=std::snprintf(detail+used,sizeof detail-used," %u.%u=%.2f",slot/256u,slot%256u,double(methodTime[slot].count())/1e6/frames);
            // Per guest thread, per frame: ms holding the machine, acquisitions, ms queued for it.
            std::string sched;
            for(unsigned slot=0;slot<std::min(schedSlotsClaimed.load(),schedSlotCount);++slot){
                auto& s=schedStats[slot];
                uint64_t hold=s.holdNs.exchange(0,std::memory_order_relaxed),acq=s.acquisitions.exchange(0,std::memory_order_relaxed),wait=s.waitNs.exchange(0,std::memory_order_relaxed);
                if(!hold&&!acq)continue;
                char item[64];std::snprintf(item,sizeof item," sched t%u hold %.2f acq %.1f wait %.2f",slot?s.guestId.load(std::memory_order_relaxed):1u,
                    double(hold)/1e6/frames,double(acq)/frames,double(wait)/1e6/frames);
                sched+=item;
            }
            uint64_t constantSets=0,constantHits=0;d3d9ShaderConstantStats(&constantSets,&constantHits);
            NFS_RUNTIME_LOG(ANDROID_LOG_INFO,"NFSU2","Bridge ms/frame by kind.method:%s; shader constants %.0f/frame, %.0f%% duplicate; render handovers %.1f/frame, waiting %.2f ms/frame;%s",used?detail:" none",
                double(constantSets)/frames,constantSets?100.0*double(constantHits)/double(constantSets):0.0,
                double(renderYields)/frames,double(renderWait.count())/1e6/frames,sched.c_str());
            renderYields=0;renderWait=std::chrono::nanoseconds{};
            methodTime.fill(std::chrono::nanoseconds{});
            frames=0;since=now;bridgeTime=presentTime=std::chrono::nanoseconds{};bridgeCalls=0;}
    }ret(result,count);}
#endif
// threadStart: a thread procedure may be stdcall or cdecl; Windows exits the thread right after either.
uint32_t callGuest(uint32_t va,const uint32_t* args,size_t count,uint32_t cleanup,bool threadStart);
// Braced argument lists stay on the host stack: audio and timer callbacks run
// many times a frame and a std::vector per call cost a malloc/free pair.
inline uint32_t callGuest(uint32_t va,std::initializer_list<uint32_t> args,uint32_t cleanup=0,bool threadStart=false){return callGuest(va,args.begin(),args.size(),cleanup,threadStart);}
inline uint32_t callGuest(uint32_t va,const std::vector<uint32_t>& args,uint32_t cleanup=0,bool threadStart=false){return callGuest(va,args.data(),args.size(),cleanup,threadStart);}
#include "guest_threads.inc"
#include "guest_files.inc"
#include "guest_kernel.inc"
#include "guest_input.inc"
#include "guest_audio.inc"
#include "guest_network.inc"
void testGuestScheduling(){
    State original;original.save();uint32_t savedError=lastError,savedTLS=tlsValues[1087];
    g_eax=0x11223344;g_fs_base=0x21000000;g_st[0]=123.5;g_xmm[7].u64[0]=0x123456789abcdef0ull;
    lastError=111;tlsValues[1087]=0x55;
    bool done=false;std::string failure;
    std::thread worker([&]{
        std::unique_lock<MachineMutex> lease(machine);machineLease=&lease;guestThreadId=9000;
        State fresh;fresh.eax=0xaabbccdd;fresh.fs=0xdead1000;fresh.st[0]=-456.25;fresh.xmm[7].u64[0]=0xfedcba9876543210ull;fresh.load();
        lastError=222;tlsValues[1087]=0xaa;
        try{for(unsigned i=0;i<100;++i){checkGuestProgress();yieldGuestMachine();
            if(g_eax!=0xaabbccdd||g_fs_base!=0xdead1000||g_st[0]!=-456.25||g_xmm[7].u64[0]!=0xfedcba9876543210ull||lastError!=222||tlsValues[1087]!=0xaa)
                throw GuestStop("Guest worker context was not isolated");}}
        catch(const std::exception& error){failure=error.what();}
        forgetThreadTrace();done=true;machineLease=nullptr;
    });
    try{while(!done){checkGuestProgress();yieldGuestMachine(0,std::chrono::steady_clock::now()+std::chrono::milliseconds(1));
        if(g_eax!=0x11223344||g_fs_base!=0x21000000||g_st[0]!=123.5||g_xmm[7].u64[0]!=0x123456789abcdef0ull||lastError!=111||tlsValues[1087]!=0x55)
            failure="Main guest context was not preserved during handover";
    }}catch(const std::exception& error){failure=error.what();}
    machineLease->unlock();worker.join();machineLease->lock();original.load();lastError=savedError;tlsValues[1087]=savedTLS;
    if(!failure.empty())throw GuestStop(failure);
    NFS_RUNTIME_LOG(ANDROID_LOG_INFO,"NFSU2","Guest scheduler passed 100 handovers with separate integer, FS, x87, XMM, TLS and last-error state");
}
void testGuestSoundCaps(){
    State saved;saved.save();
    uint32_t output=allocate(96),device=createSoundObject(1);
    write32(output,96);
    uint32_t result=callGuest(soundMethodBase+0x1000+4*16,{device,output});
    bool ok=result==0&&read32(output)==96&&read32(output+4)==0xf1f&&read32(output+8)==100&&
        read32(output+12)==200000&&read32(output+16)==1&&read32(output+20)==0;
    callGuest(soundMethodBase+0x1000+2*16,{device});heaps->free(GuestHeaps::process,output);saved.load();
    if(!ok)throw GuestStop("DirectSound PCM capabilities regression");
    NFS_RUNTIME_LOG(ANDROID_LOG_INFO,"NFSMW","DirectSound caps passed: mono/stereo, 8/16-bit, continuous rates, software buffers and guest ABI");
}
recomp_func_t resolveFunction(const std::string& dll, const std::string& name) {
    if((dll=="shfolder.dll"||dll=="shell32.dll")&&name=="SHGetFolderPathA")return shellFolderPath;
    if(dll=="dsound.dll"&&(name=="#1"||name=="DirectSoundCreate"))return directSoundCreate;
    if(dll=="dinput8.dll"&&name=="DirectInput8Create")return directInputCreate;
    if(dll=="gdi32.dll"&&name=="DeleteObject")return deleteGdiObject;
#if defined(__ANDROID__) || defined(NFS_D3D9_BACKEND)
    if(dll=="d3d9.dll"&&name=="Direct3DCreate9")return direct3DCreate;
#endif
    if(dll=="advapi32.dll"&&name=="RegOpenKeyA")return regOpenKeyA;
    if(dll=="advapi32.dll"&&name=="RegOpenKeyExA")return regOpenKeyExA;
    if(dll=="advapi32.dll"&&name=="RegCloseKey")return regCloseKey;
    if(dll=="advapi32.dll"&&name=="RegQueryValueExA")return regQueryValueExA;
    if(dll=="advapi32.dll"&&name=="RegSetValueExA")return regSetValueExA;
    if(dll=="ws2_32.dll"||dll=="wsock32.dll"){auto found=networkFunctions.find(name);if(found!=networkFunctions.end())return found->second;}
    if(dll=="netapi32.dll"&&name=="Netbios")return netbios;
    if(dll=="tapi32.dll"&&name=="lineInitialize")return lineInitialize;
    if(dll=="tapi32.dll"&&name=="lineShutdown")return lineShutdown;
    if(dll=="user32.dll") {auto found=windowFunctions.find(name);if(found!=windowFunctions.end())return found->second;}
    if(dll=="winmm.dll") {auto found=timerFunctions.find(name);if(found!=timerFunctions.end())return found->second;}
    if (dll == "kernel32.dll") {
        auto resource=resourceFunctions.find(name);if(resource!=resourceFunctions.end())return resource->second;
        auto file=fileFunctions.find(name);if(file!=fileFunctions.end())return file->second;
        auto kernel=kernelFunctions.find(name); if(kernel!=kernelFunctions.end()) return kernel->second;
        if (name=="GetVersionExA") return getVersion;
        if (name=="GetModuleHandleA") return moduleHandle;
        if (name=="HeapCreate") return heapCreate;
        if (name=="HeapAlloc") return heapAlloc;
        if (name=="HeapFree") return heapFree;
        if (name=="HeapReAlloc") return heapRealloc;
        if (name=="HeapSize") return heapSize;
        if (name=="HeapDestroy") return heapDestroy;
        if (name=="GetProcessHeap") return getProcessHeap;
    }
    return unsupported;
}
void getProcAddress() {
    uint32_t module=arg(0),namePointer=arg(1);
    auto found=std::find_if(modules.begin(),modules.end(),[&](auto& m){return m.second==module;});
    auto name=namePointer<=0xffff?"#"+std::to_string(namePointer):guestString(namePointer);
    if(uint32_t va;found==modules.end()&&guestModuleExport(module,name,va)) { ret(va,2); return; }
    if(found==modules.end()) { lastError=isGuestModule(module)?127:126; ret(0,2); return; }
    auto full=found->first+"!"+name;
    for(uint32_t i=0;i<imports.size();++i) if(imports[i].name==full) { ret(importBase+i*16,2); return; }
    // Only advertise exports that are actually implemented, not permissive stubs.
    auto fn=resolveFunction(found->first,name);
    if(fn==unsupported) { lastError=127; ret(0,2); return; }
    uint32_t va=importBase+uint32_t(imports.size())*16;
    imports.push_back({full,fn}); ret(va,2);
}

template<class T> T fileStruct(const std::vector<unsigned char>& file, uint64_t offset) {
    if (offset > file.size() || sizeof(T) > file.size()-offset) throw GuestStop("Truncated PE structure");
    T value{}; std::memcpy(&value,file.data()+offset,sizeof value); return value;
}
void moduleRange(uint32_t size, uint32_t rva, uint64_t bytes) {
    if (rva > size || bytes > uint64_t(size)-rva) throw GuestStop("PE image range invalid");
}
std::string moduleString(uint32_t base, uint32_t size, uint32_t rva) {
    moduleRange(size,rva,1); std::string value;
    for (uint32_t i=rva; i<size && value.size()<512; ++i) {
        char c = *static_cast<char*>(ptr(base+i,1)); if (!c) return value; value.push_back(c);
    }
    throw GuestStop("Unterminated PE import name");
}
void imageRange(uint32_t rva, uint64_t bytes) { moduleRange(imageSize,rva,bytes); }
// Writes an import token into every IAT slot of a mapped image (SPEED2.EXE or a game DLL).
void bindImports(uint32_t base, uint32_t size, uint32_t directoryRva, uint32_t directorySize) {
    moduleRange(size,directoryRva,directorySize);
    bool terminated=false;
    for (uint32_t offset=0; uint64_t(offset)+sizeof(pe_import_descriptor)<=directorySize; offset+=sizeof(pe_import_descriptor)) {
        pe_import_descriptor descriptor{};
        std::memcpy(&descriptor,ptr(base+directoryRva+offset,sizeof descriptor),sizeof descriptor);
        if (!descriptor.Name && !descriptor.FirstThunk) { terminated=true; break; }
        auto dll=moduleString(base,size,descriptor.Name);
        std::transform(dll.begin(),dll.end(),dll.begin(),[](unsigned char c){return char(std::tolower(c));});
        if(!modules.count(dll)) modules[dll]=0x71000000u+uint32_t(modules.size())*0x10000u;
        uint32_t names=descriptor.OriginalFirstThunk ? descriptor.OriginalFirstThunk : descriptor.FirstThunk;
        for (uint32_t i=0;;++i) {
            uint64_t nameRva=uint64_t(names)+i*4ull, iatRva=uint64_t(descriptor.FirstThunk)+i*4ull;
            if (nameRva>UINT32_MAX || iatRva>UINT32_MAX) throw GuestStop("PE thunk overflow");
            moduleRange(size,uint32_t(nameRva),4); moduleRange(size,uint32_t(iatRva),4);
            uint32_t thunk=read32(base+uint32_t(nameRva)); if (!thunk) break;
            std::string name;
            if (thunk&PE_ORDINAL_FLAG) name="#"+std::to_string(thunk&0xffff);
            else { moduleRange(size,thunk,3); name=moduleString(base,size,thunk+2); }
            if (imports.size()>=4096) throw GuestStop("Too many PE imports");
            uint32_t va=importBase+uint32_t(imports.size())*16;
            imports.push_back({dll+"!"+name,resolveFunction(dll,name)});
            write32(base+uint32_t(iatRva),va);
        }
    }
    if (!terminated) throw GuestStop("Unterminated PE import descriptors");
}
void loadImage(const char* path) {
    std::ifstream stream(path,std::ios::binary);
    if (!stream) throw GuestStop("Cannot open game PE");
    std::vector<unsigned char> file((std::istreambuf_iterator<char>(stream)),{});
    auto dos=fileStruct<pe_dos_header>(file,0);
    if (dos.e_magic!=0x5a4d || dos.e_lfanew<0) throw GuestStop("Invalid DOS header");
    auto nt=fileStruct<pe_nt_headers32>(file,dos.e_lfanew);
    if (nt.Signature!=0x4550 || nt.FileHeader.Machine!=0x14c || nt.OptionalHeader.Magic!=0x10b ||
        nt.FileHeader.SizeOfOptionalHeader<sizeof(pe_opt_header32)) throw GuestStop("Expected x86 PE32");
    auto opt=nt.OptionalHeader; imageBase=opt.ImageBase; imageSize=opt.SizeOfImage;
    resourceRva=opt.DataDirectory[2].VirtualAddress;resourceSize=opt.DataDirectory[2].Size;
    if (imageBase!=0x400000 || imageSize!=0x532000 || opt.AddressOfEntryPoint!=0x35bcc7)
        throw GuestStop("PE layout does not match generated NFSU2 code");
    memory->commit(imageBase,imageSize);
    imageRange(0,opt.SizeOfHeaders);
    if (opt.SizeOfHeaders>file.size()) throw GuestStop("Truncated PE headers");
    std::memcpy(ptr(imageBase,opt.SizeOfHeaders),file.data(),opt.SizeOfHeaders);
    uint64_t sections=uint64_t(dos.e_lfanew)+24+nt.FileHeader.SizeOfOptionalHeader;
    for (uint32_t i=0;i<nt.FileHeader.NumberOfSections;++i) {
        auto sec=fileStruct<pe_section_header>(file,sections+i*sizeof(pe_section_header));
        imageRange(sec.VirtualAddress,std::max(sec.VirtualSize,sec.SizeOfRawData));
        if (uint64_t(sec.PointerToRawData)+sec.SizeOfRawData>file.size()) throw GuestStop("Truncated PE section");
        std::memcpy(ptr(imageBase+sec.VirtualAddress,sec.SizeOfRawData),file.data()+sec.PointerToRawData,sec.SizeOfRawData);
    }
    entry=imageBase+opt.AddressOfEntryPoint;
    // Guest VAs stay fixed; the 64-bit host base is ADDR's offset. No PE
    // relocation toward the host address and no executable x86 mapping.
    auto directory=opt.DataDirectory[PE_DIR_IMPORT];
    bindImports(imageBase,imageSize,directory.VirtualAddress,directory.Size);
    NFS_RUNTIME_LOG(ANDROID_LOG_INFO,"NFSU2","Guest PE mapped: base=%08x size=%08x entry=%08x; %zu imports bound; %u lifted functions",
        imageBase,imageSize,entry,imports.size(),recomp_dispatch_count);
}
uint32_t callGuest(uint32_t va, const uint32_t* args, size_t count, uint32_t cleanup, bool threadStart) {
    State saved; saved.save();
    try {
        recomp_func_t fn=recomp_lookup_manual(va);if(!fn)fn=recomp_lookup(va);
        if (!fn) fn=recomp_lookup_import(va);
        if (!fn) throw GuestStop("No guest function at " + hex(va));
        for (size_t i=count;i--;) { g_esp-=4; write32(g_esp,args[i]); }
        g_esp-=4; write32(g_esp,RECOMP_RETADDR);
        fn(); g_esp+=cleanup;
        if (g_esp!=saved.esp&&!(threadStart&&g_esp+4u*uint32_t(count)==saved.esp)) throw GuestStop("Guest call stack imbalance at " + hex(va));
        uint32_t result=g_eax; saved.load(); return result;
    } catch (...) { saved.load(); throw; }
}
#include "guest_modules.inc"
void testLiftedCopy() {
    uint32_t source=allocate(128), dest=allocate(128);
    for (unsigned i=0;i<128;++i) static_cast<unsigned char*>(ptr(source,128))[i]=static_cast<unsigned char>(i^0xa5);
    g_esi=0x11111111; g_edi=0x22222222; g_xmm[7].u64[0]=0xfedcba9876543210ull;
    auto stack=g_esp;
    for (uint32_t length : {0u,1u,3u,4u,7u,63u,127u}) {
        std::memset(ptr(dest,128),0xcc,128);
        auto result=callGuest(0x401000,{dest,source,length},12);
        if (result!=length || std::memcmp(ptr(dest,length),ptr(source,length),length) ||
            static_cast<unsigned char*>(ptr(dest,128))[length]!=0xcc || g_esp!=stack ||
            g_esi!=0x11111111 || g_edi!=0x22222222 || g_xmm[7].u64[0]!=0xfedcba9876543210ull)
            throw GuestStop("Lifted copy/callback state validation failed");
    }
    NFS_RUNTIME_LOG(ANDROID_LOG_INFO,"NFSU2","Real lifted sub_00401000 passed 7 copy/cdecl stack cases on a 64-bit host");
}
void timerTestCallback(){timerTestCalls.push_back(arg(0));ret(0,5);}
void testGuestTimers(){
    auto imported=[&](const std::string& name){for(uint32_t i=0;i<imports.size();++i)if(imports[i].name==name)return importBase+i*16;throw GuestStop("Timer test import unavailable");};
    uint32_t set=imported("winmm.dll!timeSetEvent"),kill=imported("winmm.dll!timeKillEvent");
    auto wait=[&](unsigned count){auto until=std::chrono::steady_clock::now()+std::chrono::seconds(2);while(timerTestCalls.size()<count){if(std::chrono::steady_clock::now()>until)throw GuestStop("Timer callback deadline failed");checkGuestProgress();yieldGuestMachine(1);}};
    timerTestActive=true;timerTestCalls.clear();
    try{
        auto first=callGuest(set,{2,0,timerTestAddress,0,0x100});if(!first)throw GuestStop("Timer creation failed");wait(1);
        if(timerTestCalls[0]!=first||timerHandles.count(first))throw GuestStop("One-shot timer retirement failed");
        auto worker=timerWorker->id;
        auto periodic=callGuest(set,{2,0,timerTestAddress,0,0x101});wait(3);
        if(callGuest(kill,{periodic})||timerHandles.count(periodic))throw GuestStop("Periodic timer cancellation failed");
        size_t after=timerTestCalls.size();auto cancelled=callGuest(set,{20,0,timerTestAddress,0,0x100});
        if(callGuest(kill,{cancelled}))throw GuestStop("Pending timer cancellation failed");
        auto until=std::chrono::steady_clock::now()+std::chrono::milliseconds(25);while(std::chrono::steady_clock::now()<until){checkGuestProgress();yieldGuestMachine(1);}
        if(timerTestCalls.size()!=after||!timerHandles.empty()||timerWorker->id!=worker||guestThreads.size()!=1)throw GuestStop("Timer cancellation/worker reuse failed");
        timerTestActive=false;
        NFS_RUNTIME_LOG(ANDROID_LOG_INFO,"NFSU2","Timer worker passed: one-shot, periodic, synchronous cancel, pending cancel, retirement and single-thread reuse");
    }catch(...){timerTestActive=false;for(auto& timer:timerHandles)timer.second->cancelled=true;timerHandles.clear();throw;}
}
void testGuestFileTime(){
    auto imported=[&](const std::string& name){for(uint32_t i=0;i<imports.size();++i)if(imports[i].name==name)return importBase+i*16;throw GuestStop("File-time import unavailable");};
    uint32_t local=imported("kernel32.dll!FileTimeToLocalFileTime"),system=imported("kernel32.dll!FileTimeToSystemTime");
    uint32_t data=allocate(48);uint64_t epoch=116444736000000000ull+1234ull*10000ull;
    std::memset(ptr(data,48),0xa5,48);std::memcpy(ptr(data,8),&epoch,8);
    bool okay=callGuest(local,{data,data+8})==1&&read32(data+8)==uint32_t(epoch)&&read32(data+12)==uint32_t(epoch>>32)
        &&callGuest(system,{data+8,data+16})==1;
    const uint16_t expected[]={1970,1,4,1,0,0,1,234};
    okay=okay&&!std::memcmp(ptr(data+16,16),expected,16)&&read32(data+32)==0xa5a5a5a5u;
    okay=okay&&callGuest(local,{data,data})==0;
    uint64_t invalid=0x8000000000000000ull;std::memcpy(ptr(data,8),&invalid,8);
    okay=okay&&callGuest(system,{data,data+16})==0;
    heaps->free(GuestHeaps::process,data);
    if(!okay)throw GuestStop("Guest file-time conversion self-check failed");
    NFS_RUNTIME_LOG(ANDROID_LOG_INFO,"NFSU2","Guest UTC FILETIME conversions passed: epoch, milliseconds, guards and invalid input");
}
void testGuestCriticalSections(){
    auto imported=[&](const std::string& name){for(uint32_t i=0;i<imports.size();++i)if(imports[i].name=="kernel32.dll!"+name)return importBase+i*16;throw GuestStop("Critical-section import unavailable");};
    uint32_t initialize=imported("InitializeCriticalSection"),enter=imported("EnterCriticalSection"),leave=imported("LeaveCriticalSection"),remove=imported("DeleteCriticalSection");
    uint32_t data=allocate(32);std::memset(ptr(data,32),0xa5,32);
    callGuest(initialize,{data});callGuest(enter,{data});callGuest(enter,{data});
    bool okay=read32(data+8)==2&&read32(data+12)==guestThreadId&&read32(data+24)==0xa5a5a5a5u;
    callGuest(remove,{data});okay=okay&&criticalSections.count(data)&&pendingCriticalDeletes.count(data);
    callGuest(leave,{data});okay=okay&&read32(data+8)==1&&criticalSections.count(data);
    callGuest(leave,{data});okay=okay&&!criticalSections.count(data)&&!pendingCriticalDeletes.count(data)&&read32(data+8)==0;
    callGuest(initialize,{data});callGuest(remove,{data});okay=okay&&!criticalSections.count(data);
    heaps->free(GuestHeaps::process,data);
    if(!okay)throw GuestStop("Guest critical-section retirement self-check failed");
    NFS_RUNTIME_LOG(ANDROID_LOG_INFO,"NFSU2","Guest critical sections passed recursion, owner, guards, deferred retirement and reinitialization");
}
uint32_t dynamicThunkAddress{};
void generatedTestCallback(){++generatedTestCalls;ret(read32(arg(0)),0);}

namespace {
// dynamicThunk interprets a small, fixed instruction set the game's own audio mix
// graph compiler emits (push esi; call f; mov [esi+n],eax; ..., about 100 blocks per
// mix) plus a short arithmetic tail. Re-parsing that from guest memory on every one of
// the ~100 calls a mix made per invocation, every time the mix plays, was ~10% of the
// audio thread (see docs/AUDIO_FRAME_PACING.md) -- which holds the guest machine the
// render thread waits on. An address is decoded into this graph once; later calls walk
// the graph directly (no byte reads besides the integrity check below, no opcode
// dispatch, no recomp_lookup per call site). Branches stay data-driven: only *decoding*
// is cached, Jcc is still evaluated from live registers on every replay, so the existing
// self-test (testGeneratedCallbacks, same start address, three different inputs
// including both sides of its min/max branch) still exercises this correctly.
struct ThunkOp {
    enum Kind : uint8_t { Skip, Call, AddEsi, Store, ReloadEsi, LoadEaxOff, LoadEcxOff,
        EaxFromEcx, Cmp, Imul, Jcc, ZeroByte, AddOff, SubOff, Ret } kind;
    uint32_t a=0;              // offset/delta (most kinds), or the call target VA (Call)
    uint32_t next=0;           // fallthrough pc; unused for Ret
    uint32_t taken=0;          // Jcc only: pc when the branch is taken
    uint8_t cc=0;              // Jcc only: the 0x7c..0x7f opcode byte
    recomp_func_t fn=nullptr;  // Call only: resolved once, reused on every replay
};
// program: decoded by guest address. ops: the same instructions linked by index
// (next/taken are positions in ops) so a replay walks an array instead of hashing
// the pc on every step -- that lookup was a third of the audio/streaming thread.
struct CachedThunk { std::vector<uint8_t> bytes; std::unordered_map<uint32_t,ThunkOp> program; std::vector<ThunkOp> ops; uint32_t entry=0; };
void thunkLink(CachedThunk& cached,uint32_t start){
    std::unordered_map<uint32_t,uint32_t> index;index.reserve(cached.program.size());
    cached.ops.clear();cached.ops.reserve(cached.program.size());
    for(const auto& [pc,op]:cached.program){index.emplace(pc,uint32_t(cached.ops.size()));cached.ops.push_back(op);}
    for(auto& op:cached.ops){
        if(op.kind!=ThunkOp::Ret)op.next=index.at(op.next);
        if(op.kind==ThunkOp::Jcc)op.taken=index.at(op.taken);
    }
    cached.entry=index.at(start);
}
std::unordered_map<uint32_t,CachedThunk> g_thunkCache;

// Decode-time-only bookkeeping: which opcodes are legal next (this is how the original
// interpreter validated the generated code's shape). None of it survives into replay.
struct ThunkParse { unsigned saved=0,arguments=0,totalArguments=0; bool pending=false,stackCleared=false,popped=false,compared=false; };

void thunkDecodeFault(uint32_t start,uint32_t at){
    if(memory->readable(at,32)){
        const auto* raw=static_cast<const uint8_t*>(ptr(at,32));std::ostringstream dump;
        for(unsigned i=0;i<32;++i)dump<<std::hex<<std::setw(2)<<std::setfill('0')<<unsigned(raw[i]);
        NFS_RUNTIME_LOG(ANDROID_LOG_WARN,"NFSU2","Generated callback start=%08x offset=%03x bytes=%s",start,at-start,dump.str().c_str());
    }
    throw GuestStop("Generated callback opcode pending at "+hex(at));
}

// Decodes every instruction reachable from `pc` (the starting state's own run, plus
// whatever a Jcc it meets branches to) until each path rejoins an already-decoded
// instruction or returns. A guard bounds total instructions decoded across every call,
// mirroring the original interpreter's own per-call step budget.
void thunkDecodeFrom(uint32_t pc,ThunkParse state,uint32_t start,CachedThunk& cached,unsigned& guard,uint32_t& highestEnd){
    for(;;){
        if(cached.program.count(pc))return;
        if(++guard>8192)throw GuestStop("Generated callback did not return at "+hex(start));
        if(pc<start||pc-start>=65536)throw GuestStop("Generated callback branch outside budget");
        uint32_t instruction=pc;
        auto byte=[&](){return *static_cast<const uint8_t*>(ptr(pc++,1));};
        auto word=[&](){uint32_t value=read32(pc);pc+=4;return value;};
        uint8_t op=byte();
        ThunkOp decoded{};
        if(op==0x56){
            if(!state.saved)state.saved=1;else{++state.arguments;++state.totalArguments;state.pending=true;state.stackCleared=false;}
            decoded.kind=ThunkOp::Skip;
        }else if(op==0xe8&&state.pending){
            int32_t delta=static_cast<int32_t>(word());uint32_t target=pc+delta;
            recomp_func_t fn=generatedTestActive&&target==generatedTestTarget?generatedTestCallback:recomp_lookup(target);
            if(!fn)throw GuestStop("Generated callback calls unlifted "+hex(target));
            decoded.kind=ThunkOp::Call;decoded.a=target;decoded.fn=fn;state.pending=false;
        }else if(op==0x83||op==0x81){
            uint8_t form=byte();uint32_t delta=op==0x83?uint32_t(int32_t(int8_t(byte()))):word();
            if(form==0xc6){decoded.kind=ThunkOp::AddEsi;decoded.a=delta;state.compared=false;}
            else if(form==0xc4&&delta==state.arguments*4&&!state.pending){decoded.kind=ThunkOp::Skip;state.stackCleared=true;state.arguments=0;state.compared=false;}
            else thunkDecodeFault(start,instruction);
        }else if(op==0x89){
            uint8_t form=byte();uint32_t offset;
            if(form==0x86)offset=word();else if(form==0x46)offset=byte();else{thunkDecodeFault(start,instruction);offset=0;}
            decoded.kind=ThunkOp::Store;decoded.a=offset;
        }else if(op==0x8b){
            uint8_t form=byte();
            if(form==0x74||form==0xb4){uint8_t sib=byte();uint32_t offset=form==0x74?uint32_t(int32_t(int8_t(byte()))):word();
                if(sib==0x24&&state.saved&&offset==4*(state.saved+state.arguments)+4)decoded.kind=ThunkOp::ReloadEsi;
                else thunkDecodeFault(start,instruction);
            }else if(form==0x46){decoded.kind=ThunkOp::LoadEaxOff;decoded.a=uint32_t(int32_t(int8_t(byte())));}
            else if(form==0x06){decoded.kind=ThunkOp::LoadEaxOff;decoded.a=0;}
            else if(form==0x4e){decoded.kind=ThunkOp::LoadEcxOff;decoded.a=uint32_t(int32_t(int8_t(byte())));}
            else if(form==0x8e){decoded.kind=ThunkOp::LoadEcxOff;decoded.a=word();}
            else if(form==0x0e){decoded.kind=ThunkOp::LoadEcxOff;decoded.a=0;}
            else if(form==0xc1){decoded.kind=ThunkOp::EaxFromEcx;}
            else thunkDecodeFault(start,instruction);
        }else if(op==0x3b){
            if(byte()!=0xc1)thunkDecodeFault(start,instruction);
            decoded.kind=ThunkOp::Cmp;state.compared=true;
        }else if(op==0x0f){
            if(byte()!=0xaf)thunkDecodeFault(start,instruction);
            uint8_t form=byte();
            if(form==0x46){decoded.kind=ThunkOp::Imul;decoded.a=uint32_t(int32_t(int8_t(byte())));}
            else if(form==0x86){decoded.kind=ThunkOp::Imul;decoded.a=word();}
            else if(form==0x06){decoded.kind=ThunkOp::Imul;decoded.a=0;}
            else thunkDecodeFault(start,instruction);
            state.compared=false;
        }else if(op>=0x7c&&op<=0x7f&&state.compared){
            int8_t delta=int8_t(byte());uint32_t target=pc+int32_t(delta);
            if(target<start||target-start>=65536)throw GuestStop("Generated callback branch outside budget");
            decoded.kind=ThunkOp::Jcc;decoded.cc=op;decoded.taken=target;decoded.next=pc;
            cached.program[instruction]=decoded;highestEnd=std::max(highestEnd,pc);
            thunkDecodeFrom(target,state,start,cached,guard,highestEnd);
            continue;  // decode the fallthrough path (pc already advanced past the branch)
        }else if(op==0xc6){
            uint8_t b1=byte(),b2=byte(),b3=byte();
            if(b1!=0x46||b2!=0||b3!=0)thunkDecodeFault(start,instruction);
            decoded.kind=ThunkOp::ZeroByte;
        }else if(op==0x03||op==0x2b){
            uint8_t form=byte();uint32_t offset;
            if(form==0x46)offset=uint32_t(int32_t(int8_t(byte())));else if(form==0x86)offset=word();else if(form==0x06)offset=0;
            else{thunkDecodeFault(start,instruction);offset=0;}
            decoded.kind=op==0x03?ThunkOp::AddOff:ThunkOp::SubOff;decoded.a=offset;state.compared=false;
        }else if(op==0x5e&&state.stackCleared&&!state.pending){
            decoded.kind=ThunkOp::Skip;state.popped=true;
        }else if(op==0xc3&&state.popped&&state.saved&&state.totalArguments>0&&!state.arguments){
            decoded.kind=ThunkOp::Ret;cached.program[instruction]=decoded;highestEnd=std::max(highestEnd,pc);return;
        }else{
            thunkDecodeFault(start,instruction);
        }
        decoded.next=pc;cached.program[instruction]=decoded;highestEnd=std::max(highestEnd,pc);
    }
}

void thunkReplay(const CachedThunk& cached,uint32_t start){
    uint32_t originalEsi=g_esi,esi=0,eax=g_eax,ecx=g_ecx,originalEsp=g_esp;int comparison=0;
    const ThunkOp* ops=cached.ops.data();uint32_t pc=cached.entry;
    for(unsigned steps=0;steps<4096;++steps){
        const ThunkOp& op=ops[pc];
        switch(op.kind){
        case ThunkOp::Skip: pc=op.next; break;
        case ThunkOp::AddEsi: esi+=op.a; pc=op.next; break;
        case ThunkOp::Store: write32(esi+op.a,eax); pc=op.next; break;
        case ThunkOp::ReloadEsi: esi=read32(originalEsp+4); pc=op.next; break;
        case ThunkOp::LoadEaxOff: eax=read32(esi+op.a); pc=op.next; break;
        case ThunkOp::LoadEcxOff: ecx=read32(esi+op.a); pc=op.next; break;
        case ThunkOp::EaxFromEcx: eax=ecx; pc=op.next; break;
        case ThunkOp::Cmp: comparison=int32_t(eax)<int32_t(ecx)?-1:int32_t(eax)>int32_t(ecx)?1:0; pc=op.next; break;
        case ThunkOp::Imul: eax*=read32(esi+op.a); pc=op.next; break;
        case ThunkOp::Jcc:{
            bool taken=op.cc==0x7c?comparison<0:op.cc==0x7d?comparison>=0:op.cc==0x7e?comparison<=0:comparison>0;
            pc=taken?op.taken:op.next; break;}
        case ThunkOp::ZeroByte: *static_cast<uint8_t*>(ptr(esi,1))=0; pc=op.next; break;
        case ThunkOp::AddOff: eax+=read32(esi+op.a); pc=op.next; break;
        case ThunkOp::SubOff: eax-=read32(esi+op.a); pc=op.next; break;
        case ThunkOp::Call:{
            // A plain cdecl call, as the x86 code does: the lifted callee keeps
            // ebx/esi/edi/ebp itself. callGuest's full register/x87/SSE save and
            // restore around each of these ~100 calls per mix dominated the
            // audio thread, which holds the guest machine the renderer waits on.
            uint32_t caller=g_cur_func,stack=g_esp;
            g_esp-=4;write32(g_esp,esi);g_esp-=4;write32(g_esp,RECOMP_RETADDR);
            op.fn();g_esp+=4;g_cur_func=caller;
            if(g_esp!=stack)throw GuestStop("Generated callback stack imbalance calling "+hex(op.a));
            eax=g_eax;pc=op.next;break;}
        case ThunkOp::Ret:
            g_eax=eax;g_ecx=ecx;g_esi=originalEsi;g_esp=originalEsp+4;return;
        }
    }
    throw GuestStop("Generated callback did not return at "+hex(start));
}
}

void dynamicThunk(){
    uint32_t start=dynamicThunkAddress;
    if(!memory->readable(start,5)||read32(start)!=0x24748b56u||*static_cast<uint8_t*>(ptr(start+4,1))!=8)
        throw GuestStop("Invalid generated callback at "+hex(start));
    auto found=g_thunkCache.find(start);
    if(found!=g_thunkCache.end()){
        const CachedThunk& cached=found->second;
        // The guest heap can reuse this address for different generated code later in
        // the session (a mix's thunk freed, another's allocated in its place): trust the
        // cached decode only while the bytes it was built from are still there.
        if(memory->readable(start,uint32_t(cached.bytes.size()))&&
           std::memcmp(ptr(start,cached.bytes.size()),cached.bytes.data(),cached.bytes.size())==0){
            thunkReplay(cached,start);return;
        }
        g_thunkCache.erase(found);
    }
    CachedThunk cached;unsigned guard=0;uint32_t highestEnd=start;
    thunkDecodeFrom(start,ThunkParse{},start,cached,guard,highestEnd);
    // highestEnd is the exact end of the last byte any decoded instruction consumed:
    // the integrity check below only needs to cover bytes this decode actually read.
    uint32_t length=highestEnd-start;
    cached.bytes.assign(static_cast<const uint8_t*>(ptr(start,length)),static_cast<const uint8_t*>(ptr(start,length))+length);
    thunkLink(cached,start);
    static unsigned completed=0;if(++completed<=3)NFS_RUNTIME_LOG(ANDROID_LOG_INFO,"NFSU2","Generated x86 callback decoded: %08x, %zu operations",start,cached.program.size());
    thunkReplay(cached,start);
    g_thunkCache.emplace(start,std::move(cached));
}
void testGeneratedCallbacks(){
    uint32_t code=allocate(2048),data=allocate(840);std::vector<uint8_t> bytes;
    auto emit=[&](std::initializer_list<uint8_t> values){bytes.insert(bytes.end(),values.begin(),values.end());};
    auto word=[&](uint32_t value){for(unsigned i=0;i<4;++i)bytes.push_back(uint8_t(value>>(i*8)));};
    emit({0x56,0x8b,0x74,0x24,0x08});
    for(unsigned i=0;i<100;++i){emit({0x56,0xe8});word(generatedTestTarget-(code+uint32_t(bytes.size())+4));emit({0x89,0x46,0x04,0x83,0xc6,0x08});}
    emit({0x8b,0xb4,0x24});word(408);emit({0x81,0xc6});word(804);
    emit({0x8b,0x06,0x8b,0x4e,0x04,0x3b,0xc1,0x7f,0x02,0x8b,0xc1,0x0f,0xaf,0x46,0x04,0x89,0x46,0x14,
        0x03,0x46,0x04,0x89,0x46,0x18,0x2b,0x46,0x04,0x89,0x46,0x1c,0x81,0xc4});word(400);emit({0x5e,0xc3});
    std::memcpy(ptr(code,bytes.size()),bytes.data(),bytes.size());std::memset(ptr(data,840),0xa5,840);
    generatedTestActive=true;generatedTestCalls=0;
    try{
        for(auto values:{std::array<int32_t,2>{-4,5},std::array<int32_t,2>{-2,-5},std::array<int32_t,2>{INT32_MAX,2}}){
            write32(data+804,uint32_t(values[0]));write32(data+808,uint32_t(values[1]));
            uint32_t expected=uint32_t(std::max(values[0],values[1]))*uint32_t(values[1]);
            if(callGuest(code,{data},4)!=expected||read32(data+824)!=expected||read32(data+828)!=expected+uint32_t(values[1])||read32(data+832)!=expected||read32(data+836)!=0xa5a5a5a5u)
                throw GuestStop("Generated callback long routine/signed clamp failed");
        }
        if(generatedTestCalls!=300)throw GuestStop("Generated callback call count failed");
    }catch(...){generatedTestActive=false;heaps->free(GuestHeaps::process,code);heaps->free(GuestHeaps::process,data);throw;}
    generatedTestActive=false;heaps->free(GuestHeaps::process,code);heaps->free(GuestHeaps::process,data);
    NFS_RUNTIME_LOG(ANDROID_LOG_INFO,"NFSU2","Generated callbacks passed 300 calls, long context reload, signed clamps, 32-bit multiply/add/subtract, stack cleanup and guards");
}
} // namespace

#ifdef NFS_SERVER_DLL
extern "C" const recomp_dispatch_entry_t server_recomp_dispatch_table[];
extern "C" const uint32_t server_recomp_dispatch_count;
#endif
namespace {
// Every indirect call and virtual method resolves its target here, so use an
// open-addressing table (built once, read-only afterwards) instead of a binary
// search over ~28k entries with a cache miss per step.
struct DispatchHash {
    static constexpr uint32_t slots=1u<<16;
    std::unique_ptr<uint32_t[]> keys{new uint32_t[slots]()};
    std::unique_ptr<recomp_func_t[]> funcs{new recomp_func_t[slots]()};
    static uint32_t slot(uint32_t va){return (va*0x9e3779b1u)>>16;}
    void add(const recomp_dispatch_entry_t* table,uint32_t count){
        for(uint32_t i=0;i<count;++i){
            uint32_t va=table[i].address,s=slot(va);
            if(va-0x7c000000u<0x04000000u)throw GuestStop("Lifted function overlaps reserved native dispatch stubs");
            while(keys[s])s=(s+1)&(slots-1);
            keys[s]=va;funcs[s]=table[i].func;
        }
    }
    DispatchHash(){
        static_assert(slots>2*(27742+1531),"dispatch hash load factor");
        add(recomp_dispatch_table,recomp_dispatch_count);
#ifdef NFS_SERVER_DLL
        add(server_recomp_dispatch_table,server_recomp_dispatch_count);
#endif
        uint32_t used=0;for(uint32_t i=0;i<slots;++i)used+=keys[i]!=0;
        if(used>slots/2)throw GuestStop("Dispatch hash over half full: "+std::to_string(used));
    }
    recomp_func_t find(uint32_t va)const{
        if(!va)return nullptr;
        for(uint32_t s=slot(va);keys[s];s=(s+1)&(slots-1))if(keys[s]==va)return funcs[s];
        return nullptr;
    }
};
}
extern "C" recomp_func_t recomp_lookup(uint32_t va) {
    // Sound/input/D3D COM and import stubs resolve in recomp_lookup_import.
    // Avoid an always-failing hash probe on every native method call. The
    // table constructor checks that no lifted module occupies this range.
    if(va-0x7c000000u<0x04000000u)return nullptr;
    static const DispatchHash hash;
    return hash.find(va);
}
extern "C" recomp_func_t recomp_lookup_manual(uint32_t va) {
    if(timerTestActive&&va==timerTestAddress)return timerTestCallback;
    if(generatedTestActive&&va==generatedTestTarget)return generatedTestCallback;
    if(va>=GuestHeaps::firstArena&&va<GuestHeaps::firstArenaEnd&&memory&&memory->readable(va,5)&&read32(va)==0x24748b56u&&*static_cast<const uint8_t*>(ptr(va+4,1))==8){
        dynamicThunkAddress=va;return dynamicThunk;
    }
    return nfs_override_lookup(va);
}
extern "C" recomp_func_t recomp_lookup_import(uint32_t va) {
    checkGuestProgress();
#if defined(__ANDROID__) || defined(NFS_D3D9_BACKEND)
    // First: ~45k of the ~50k import calls of a race frame are D3D9 methods.
    if(va>=0x7e001000u&&va<0x7e010000u&&!(va%16)){d3dToken=va;return d3dDispatch;}
#endif
    if(va>=soundMethodBase+0x1000&&va<soundMethodBase+0x3000&&!(va%16)){soundToken=va;return soundDispatch;}
    if(va>=inputMethodBase+0x1000&&va<inputMethodBase+0x5000&&!(va%16)){inputToken=va;return inputDispatch;}  // kinds 1-4: DirectInput, keyboard, mouse, gamepad
    if (va<importBase || (va-importBase)%16) return nullptr;
    uint32_t index=(va-importBase)/16;
    if (index>=imports.size()) return nullptr;
    lastImport=va;
    if(++imports[index].calls<=3)
        NFS_RUNTIME_LOG(ANDROID_LOG_INFO,"NFSU2","Guest import %s from %08x",imports[index].name.c_str(),g_cur_func);
    return imports[index].function;
}
extern "C" recomp_func_t nfs_icall_resolve(uint32_t va) {
    // Sound, input, D3D9 and import stubs (0x7c000000..0x7fffffff): nothing lifted
    // or overridden lives there, so the vtable calls of a frame skip both probes.
    if(va-0x7c000000u<0x04000000u)return recomp_lookup_import(va);
    recomp_func_t fn=recomp_lookup_manual(va);if(fn)return fn;
    // Direct-mapped cache of lifted targets: the vtable calls of a frame repeat
    // a few hundred addresses tens of thousands of times, each a hash probe.
    // Only static lifted functions are cached (manual lookups come first and
    // may change: test callbacks, generated thunks in the heap arena).
    struct Entry{uint32_t va;recomp_func_t fn;};
    static Entry cache[4096]{};
    Entry& entry=cache[(va^(va>>12))&4095u];
    if(entry.va==va)return entry.fn;
    fn=recomp_lookup(va);
    if(fn){entry.va=va;entry.fn=fn;return fn;}
    return recomp_lookup_import(va);
}
extern "C" void recomp_unresolved(const char* kind,uint32_t va,uint32_t from) {
    if(from==0x00725520u&&memory){
        uint32_t node=read32(0x8b446c);
        for(unsigned i=0;i<12&&node&&memory->readable(node,16);++i){
            NFS_RUNTIME_LOG(ANDROID_LOG_WARN,"NFSU2","Audio callback node=%08x next=%08x prev=%08x function=%08x data=%08x",node,read32(node),read32(node+4),read32(node+8),read32(node+12));
            node=read32(node);
        }
        if(memory->readable(va,24)){
            auto bytes=static_cast<const uint8_t*>(ptr(va,24));
            NFS_RUNTIME_LOG(ANDROID_LOG_WARN,"NFSU2","Unlifted callback bytes %08x: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x",va,
                bytes[0],bytes[1],bytes[2],bytes[3],bytes[4],bytes[5],bytes[6],bytes[7],bytes[8],bytes[9],bytes[10],bytes[11],bytes[12],bytes[13],bytes[14],bytes[15]);
            if(memory->readable(va,512))for(unsigned part=0;part<8;++part){
                std::ostringstream dump;for(unsigned i=part*64;i<(part+1)*64;++i)dump<<std::hex<<std::setw(2)<<std::setfill('0')<<unsigned(bytes[i]);
                NFS_RUNTIME_LOG(ANDROID_LOG_WARN,"NFSU2","Unlifted callback %03x: %s",part*64,dump.str().c_str());
            }
        }
    }
    throw GuestStop(std::string(kind)+" unresolved "+hex(va)+" from "+hex(from));
}
extern "C" void recomp_unimpl(uint32_t va,const char* what) {
    throw GuestStop("Instruction unsupported at "+hex(va)+": "+what);
}
extern "C" void recomp_dump_trace(const char*) {}
std::array<AndroidRenderMode,6> guestRenderModes{};
bool firstGameResolution=true;
void configureAndroidResolutionModes(){
    guestRenderModes=androidRenderModes(displayWidth.load(),displayHeight.load(),renderWidth.load(),renderHeight.load());
    const uint32_t labels[]={0x78fa1c,0x78fa14,0x78fa08,0x78f9fc,0x78f9f0,0x78f9e4};
    const char* original[]={"640x480","800x600","1024x768","1280x960","1280x1024","1600x1200"};
    for(unsigned i=0;i<6;++i){
        uint32_t capacity=i<2?8:12;
        if(std::strcmp(static_cast<const char*>(ptr(labels[i],capacity)),original[i]))throw GuestStop("Unexpected original resolution label");
        std::memset(ptr(labels[i],capacity),0,capacity);
        if(i<4)std::snprintf(static_cast<char*>(ptr(labels[i],capacity)),capacity,"%u%%",(i+1)*25);
        else std::snprintf(static_cast<char*>(ptr(labels[i],capacity)),capacity,"%ux%u",guestRenderModes[i].width,guestRenderModes[i].height);
        write32(0x800538+i*4,guestRenderModes[i].width);write32(0x800550+i*4,guestRenderModes[i].height);
    }
    write32(0x870d1c,5);
}
extern "C" int nfs_android_resolution(){
    uint32_t width=renderWidth.load(),height=renderHeight.load();if(!width||!height)return 0;
    if(firstGameResolution){write32(0x870d1c,5);firstGameResolution=false;}
    uint32_t index=read32(0x870d1c);if(index>=guestRenderModes.size())throw GuestStop("Invalid Android resolution index");
    width=guestRenderModes[index].width;height=guestRenderModes[index].height;
    if(!memory->writable(arg(0),4)||!memory->writable(arg(1),4))throw GuestStop("Invalid guest resolution outputs");
    write32(arg(0),width);write32(arg(1),height);ret(0,2);nfs_widescreen_set_resolution(width,height);
    NFS_RUNTIME_LOG(ANDROID_LOG_INFO,"NFSU2","Guest render resolution %ux%u slot=%u via original selector",width,height,index);
    return 1;
}

std::string connectGuestRuntime(const char* executable) {
    stayOnPerformanceCores();
    std::unique_lock<MachineMutex> guard(machine);machineLease=&guard;
    if (memory) return "Runtime guest ya inicializado";
    State original; original.save();
    try {
        stopRequested=false;
        // Interactive Android sessions stop through lifecycle cancellation.
#ifdef __ANDROID__
        bootDeadline=std::chrono::steady_clock::time_point::max();
#else
        bootDeadline=std::chrono::steady_clock::now()+std::chrono::minutes(5);
#endif
        recomp_yield_hook=checkGuestProgress;
        memory=std::make_unique<GuestMemory>();
        hostGameRoot=std::string(executable); hostGameRoot=hostGameRoot.substr(0,hostGameRoot.find_last_of('/'));
        g_mem_base=reinterpret_cast<ptrdiff_t>(memory->base());
        testGuestHeaps(*memory);
        NFS_RUNTIME_LOG(ANDROID_LOG_INFO,"NFSU2","Guest heaps passed ownership, realloc preservation, zero tail, in-place failure and reuse checks");
        heaps=std::make_unique<GuestHeaps>(*memory);
#if defined(__ANDROID__) || defined(NFS_D3D9_BACKEND)
        configureD3D9Bridge(memory->base(),allocate,freeD3DGuest);
#endif
        loadImage(executable);
        if(guestWidescreen){
            nfs_widescreen_init(hostGameRoot.c_str());
            NFS_RUNTIME_LOG(ANDROID_LOG_INFO,"NFSU2","Widescreen: %d HUD offsets from scripts/NFSUnderground2.WidescreenFix.dat",nfs_widescreen_hud_entries());
        }else NFS_RUNTIME_LOG(ANDROID_LOG_INFO,"NFSU2","Widescreen disabled: original 4:3 geometry");
        if(renderWidth.load()&&renderHeight.load())configureAndroidResolutionModes();
        memory->commit(stackBase,stackSize); memory->commit(tib,0x1000);
        State fresh; fresh.esp=stackBase+stackSize-64; fresh.fs=tib; fresh.load();
        write32(tib,0xffffffff); write32(tib+4,stackBase+stackSize); write32(tib+8,stackBase); write32(tib+0x18,tib);
        testLiftedCopy();
        testGuestScheduling();
        testGuestInput();
        testGuestSoundCaps();
        testGuestTimers();
        testGuestFileTime();
        testGuestCriticalSections();
        testGeneratedCallbacks();
        if(renderWidth.load()&&renderHeight.load()){
            uint32_t outputs=allocate(12);write32(outputs+8,0xdeadbeef);
            callGuest(0x005bf610,{outputs,outputs+4});
            if(read32(outputs)!=renderWidth.load()||read32(outputs+4)!=renderHeight.load()||read32(outputs+8)!=0xdeadbeef)throw GuestStop("Guest resolution ABI failed");
            for(uint32_t i=0;i<6;++i){write32(0x870d1c,i);callGuest(0x005bf610,{outputs,outputs+4});
                if(read32(outputs)!=guestRenderModes[i].width||read32(outputs+4)!=guestRenderModes[i].height||read32(outputs+8)!=0xdeadbeef)throw GuestStop("Guest resolution slot/guard check failed");}
            write32(0x870d1c,5);firstGameResolution=true;
            heaps->free(GuestHeaps::process,outputs);
            NFS_RUNTIME_LOG(ANDROID_LOG_INFO,"NFSU2","Guest resolution outputs, guard and stdcall stack passed");
        }
        // Restore a clean CPU state before executing the real CRT entry.
        fresh.load();
        NFS_RUNTIME_LOG(ANDROID_LOG_INFO,"NFSU2","Entering actual lifted CRT entry %08x",entry);
        uint32_t result=callGuest(entry,{});
        joinGuestThreads(guard);machineLease=nullptr;shutdownGuestSound();
#if defined(__ANDROID__) || defined(NFS_D3D9_BACKEND)
        shutdownD3D9Bridge();
#endif
        original.load();
        return "Entrada guest terminada: "+std::to_string(result);
    } catch (const std::exception& e) {
        NFS_RUNTIME_LOG(ANDROID_LOG_WARN,"NFSU2","Guest integration boundary: %s",e.what());
        joinGuestThreads(guard);machineLease=nullptr;shutdownGuestSound();
#if defined(__ANDROID__) || defined(NFS_D3D9_BACKEND)
        shutdownD3D9Bridge();
#endif
        original.load();
        return std::string("Arranque guest detenido: ")+e.what();
    }
}
void requestGuestRuntimeStop() {stopRequested=true;signalGuestSync();}
void setGuestPaused(bool paused) {
    // The AAudio stream keeps pulling from looping voices while the guest is
    // paused: in the background the last mixed sound repeated until the app
    // closed. Pause the output with the guest and restart it on resume.
    bool previous=guestPaused.exchange(paused);
    signalGuestSync();   // blocked guest threads re-check guestPaused
    if(previous!=paused&&soundStream){
        aaudio_result_t status=paused?AAudioStream_requestPause(soundStream):AAudioStream_requestStart(soundStream);
        if(status!=AAUDIO_OK)NFS_RUNTIME_LOG(ANDROID_LOG_WARN,"NFS","Audio %s failed: %d",paused?"pause":"resume",int(status));
    }
}
bool guestDrivingControls() {return inputDrivingMode.load();}
void setGuestPad(const float axes[6],uint32_t buttons,int hatX,int hatY){
    std::lock_guard<std::mutex> lock(inputMutex);
    for(unsigned i=0;i<6;++i)inputPad.axis[i]=std::max(-1.f,std::min(1.f,axes[i]));
    inputPad.buttons=buttons;inputPad.hatX=hatX;inputPad.hatY=hatY;
}
void setGuestPadConnected(bool connected){
    std::lock_guard<std::mutex> lock(inputMutex);
    if(connected!=inputPadConnected)NFS_RUNTIME_LOG(ANDROID_LOG_INFO,"NFSU2","Gamepad %s",connected?"connected":"disconnected");
    inputPadConnected=connected;if(!connected)inputPad=InputPad{};
}
bool guestGamepadInUse(){return inputPadAcquired.load()>0;}
void setGuestDisplaySize(unsigned width,unsigned height){displayWidth=width;displayHeight=height;}
void setGuestResolution(unsigned width,unsigned height){
    std::lock_guard<MachineMutex> lock(machine);if(memory)return;
    if(width<320||height<240||width>8192||height>8192){renderWidth=renderHeight=0;return;}
    renderWidth=width;renderHeight=height;
#if defined(__ANDROID__) || defined(NFS_D3D9_BACKEND)
    setD3D9RenderSize(width,height);
#endif
    NFS_RUNTIME_LOG(ANDROID_LOG_INFO,"NFSU2","Selected render resolution %ux%u",width,height);
}
void setGuestFrameLimit(unsigned framesPerSecond){
    frameLimit=(framesPerSecond>=30&&framesPerSecond<=240)?framesPerSecond:0;
    // The game paces itself to 1/60 s; let it run at the cap (uncapped: the 120 Hz display).
    nfs_widescreen_set_frame_rate(frameLimit.load()?frameLimit.load():120);
    NFS_RUNTIME_LOG(ANDROID_LOG_INFO,"NFSU2","Android frame limit %u FPS (0 means uncapped)",frameLimit.load());
}
void setGuestKey(unsigned scan,bool down){if(scan>=256)return;std::lock_guard<std::mutex> lock(inputMutex);if(bool(inputKeys[scan])==down)return;inputKeys[scan]=down?0x80:0;recordInputKey(scan,down);enqueueGuestKey(scan,down);}
void clearGuestInput(){std::lock_guard<std::mutex> lock(inputMutex);for(unsigned scan=0;scan<256;++scan)if(inputKeys[scan]){recordInputKey(scan,false);enqueueGuestKey(scan,false);}inputKeys.fill(0);inputMouseButtons.fill(0);inputMouseX=inputMouseY=inputMouseZ=0;}
void setGuestPointer(float x,float y,int phase){
    guestPointerX=std::clamp(x,0.f,1.f);guestPointerY=std::clamp(y,0.f,1.f);
    if(phase!=1){std::lock_guard<std::mutex> lock(inputMutex);inputMouseButtons[0]=phase==0?0x80:0;}
    enqueueGuestPointer(phase);
}
void typeGuestText(const std::vector<uint32_t>& characters){enqueueGuestText(characters);}
void setGuestOnlineServer(const std::string& host){std::lock_guard<MachineMutex> lock(machine);if(!memory&&!host.empty())onlineServer=host;}
void setGuestWidescreen(bool enabled){std::lock_guard<MachineMutex> lock(machine);if(!memory)guestWidescreen=enabled;}
void setGuestLanguage(const char* language){
    std::lock_guard<MachineMutex> lock(machine);if(memory)return;
    for(const char* known:{"Spanish","English UK","French","German","Italian","Dutch","Swedish","Danish","Japanese","Korean","Chinese (Traditional)","Thai"})
        if(language&&std::strcmp(known,language)==0){guestLanguage=known;NFS_RUNTIME_LOG(ANDROID_LOG_INFO,"NFSU2","Selected guest language: %s",known);return;}
    guestLanguage="Spanish";
}

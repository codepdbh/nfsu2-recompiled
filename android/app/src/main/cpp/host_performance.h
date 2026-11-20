// CPU placement for the guest. Guest threads run one at a time under the machine
// lock, so each one looks lightly loaded to the kernel scheduler even when together
// they fill a core. MediaTek's scheduler then leaves them on the little cores:
// a Dimensity 8300 reported 14 FPS at 360p, where the GPU cannot be the limit, while
// Snapdragons with less CPU kept 60. Guest threads stay on the big cores, and the
// frame thread tells the system its frame times (ADPF, Android 12+).
#pragma once
#include "runtime_log.h"
#include <dlfcn.h>
#include <sched.h>
#include <unistd.h>
#include <cstdint>
#include <cstdio>
#include <string>

inline long readCpuValue(unsigned cpu,const char* file){
    char path[96];std::snprintf(path,sizeof path,"/sys/devices/system/cpu/cpu%u/%s",cpu,file);
    long value=-1;
    if(FILE* input=std::fopen(path,"r")){if(std::fscanf(input,"%ld",&value)!=1)value=-1;std::fclose(input);}
    return value;
}
// Cores with at least 60% of the fastest one; empty when that is all of them or
// the kernel does not say (nothing to choose, so the affinity is left alone).
// Fast cores: at least 60% of the fastest one's capacity. With four or more, every
// guest thread stays on them. With fewer (2+6 designs: Helio G85/G99, capacities
// 516x6 and 1024x2), only the game's main thread does: all guest threads on two
// cores ran slower than Android's placement (U1/U2 reports), while leaving the main
// thread free made Most Wanted lag again on a Helio G99 (MW #892).
enum class CorePolicy {none,mainThread,allThreads};
inline const cpu_set_t& performanceCores(CorePolicy* policy=nullptr){
    static CorePolicy chosenPolicy=CorePolicy::none;
    static const cpu_set_t set=[]{
        constexpr unsigned maximum=32;long speed[maximum];unsigned online=0;long fastest=0;
        const char* file="cpu_capacity";
        if(readCpuValue(0,file)<=0)file="cpufreq/cpuinfo_max_freq";
        std::string report;
        for(unsigned cpu=0;cpu<maximum;++cpu){
            speed[cpu]=readCpuValue(cpu,file);
            if(speed[cpu]<=0)continue;
            ++online;fastest=std::max(fastest,speed[cpu]);
            report+=(report.empty()?"":" ")+std::to_string(speed[cpu]);
        }
        cpu_set_t chosen;CPU_ZERO(&chosen);unsigned count=0;
        for(unsigned cpu=0;cpu<maximum;++cpu)if(speed[cpu]>0&&speed[cpu]*10>=fastest*6){CPU_SET(cpu,&chosen);++count;}
        chosenPolicy=count==0||count>=online?CorePolicy::none:count>=4?CorePolicy::allThreads:CorePolicy::mainThread;
        NFS_RUNTIME_LOG(ANDROID_LOG_INFO,"NFSU2","CPU cores (%s): %s; %u fast; %s",file,report.c_str(),count,
            chosenPolicy==CorePolicy::allThreads?"guest threads on the fast cores":
            chosenPolicy==CorePolicy::mainThread?"main guest thread on the fast cores":"no affinity");
        return chosen;
    }();
    if(policy)*policy=chosenPolicy;
    return set;
}
inline void stayOnPerformanceCores(bool mainThread=false){
    CorePolicy policy=CorePolicy::none;const cpu_set_t& set=performanceCores(&policy);
    bool pin=policy==CorePolicy::allThreads||(policy==CorePolicy::mainThread&&mainThread);
    if(pin&&sched_setaffinity(0,sizeof set,&set))
        NFS_RUNTIME_LOG(ANDROID_LOG_WARN,"NFSU2","Guest thread affinity refused: errno %d",errno);
}

// APerformanceHint is API 33 in the NDK headers' sense; this APK starts at API 29.
class FrameWorkHint {
    using Manager=struct APerformanceHintManager;
    using Session=struct APerformanceHintSession;
    int (*report_)(Session*,int64_t)=nullptr;
    Session* session_=nullptr;
    bool tried_=false;
public:
    void reportFrame(int64_t workNanoseconds,int64_t targetNanoseconds){
        if(!tried_){
            tried_=true;
            void* library=dlopen("libandroid.so",RTLD_NOW|RTLD_LOCAL);
            auto getManager=library?reinterpret_cast<Manager*(*)()>(dlsym(library,"APerformanceHint_getManager")):nullptr;
            auto create=library?reinterpret_cast<Session*(*)(Manager*,const int32_t*,size_t,int64_t)>(dlsym(library,"APerformanceHint_createSession")):nullptr;
            report_=library?reinterpret_cast<int(*)(Session*,int64_t)>(dlsym(library,"APerformanceHint_reportActualWorkDuration")):nullptr;
            int32_t self=gettid();
            Manager* manager=getManager?getManager():nullptr;
            if(manager&&create&&report_)session_=create(manager,&self,1,targetNanoseconds);
            NFS_RUNTIME_LOG(ANDROID_LOG_INFO,"NFSU2","ADPF frame hints: %s",session_?"on":"not available");
        }
        if(session_&&workNanoseconds>0)report_(session_,workNanoseconds);
    }
};

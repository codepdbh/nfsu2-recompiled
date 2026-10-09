// Guest registers remain serialized; blocked native threads release the machine.
struct GuestThread {
    pthread_t native{};
    uint32_t id{},handle{},start{},parameter{},stack{},tib{},exitCode=259,suspended{};
    int32_t priority=0;
    bool started=false,done=false;
    bool timer=false;
};
struct GuestTimer {
    uint32_t id,delay,callback,parameter;bool periodic,cancelled=false,running=false,synchronous=false;
    std::chrono::steady_clock::time_point due;
};
std::map<uint32_t,std::shared_ptr<GuestTimer>> timerHandles;
std::shared_ptr<GuestThread> timerWorker;uint32_t nextTimerId=1;
thread_local uint32_t runningTimerId=0;
void runGuestTimers();
std::vector<std::shared_ptr<GuestThread>> guestThreads;
std::map<uint32_t,std::shared_ptr<GuestThread>> threadHandles;
uint32_t nextThreadId=2,nextThreadHandle=0xf0030000;
struct GuestEvent {bool manual,signaled;};
std::map<uint32_t,std::shared_ptr<GuestEvent>> eventHandles;
std::map<std::string,std::weak_ptr<GuestEvent>> namedEvents;
uint32_t nextEventHandle=0xf0040000;
struct GuestMutex {uint32_t owner=0,depth=0;bool abandoned=false;};
std::map<uint32_t,std::shared_ptr<GuestMutex>> mutexHandles;
std::map<std::string,std::weak_ptr<GuestMutex>> namedMutexes;
uint32_t nextMutexHandle=0xf0080000;
void createMutex(){
    auto name=guestString(arg(2));std::shared_ptr<GuestMutex> value;
    if(!name.empty())value=namedMutexes[name].lock();
    if(!name.empty()&&namedEvents.count(name)&&!namedEvents[name].expired()){lastError=6;ret(0,3);return;}
    lastError=value?183:0;
    if(!value){value=std::make_shared<GuestMutex>();if(arg(1)){value->owner=guestThreadId;value->depth=1;}if(!name.empty())namedMutexes[name]=value;}
    uint32_t handle=nextMutexHandle++;mutexHandles.emplace(handle,value);ret(handle,3);
}
void releaseMutex(){
    auto value=mutexHandles.find(arg(0));if(value==mutexHandles.end()){lastError=6;ret(0,1);return;}
    if(value->second->owner!=guestThreadId){lastError=288;ret(0,1);return;}
    if(!--value->second->depth)value->second->owner=0;ret(1,1);
}
void createEvent(){
    auto name=guestString(arg(3));std::shared_ptr<GuestEvent> event;
    if(!name.empty()&&namedMutexes.count(name)&&!namedMutexes[name].expired()){lastError=6;ret(0,4);return;}
    if(!name.empty())event=namedEvents[name].lock();
    lastError=event?183:0;
    if(!event){event=std::make_shared<GuestEvent>(GuestEvent{arg(1)!=0,arg(2)!=0});if(!name.empty())namedEvents[name]=event;}
    uint32_t handle=nextEventHandle++;eventHandles.emplace(handle,event);ret(handle,4);
}
void setEvent(){auto event=eventHandles.find(arg(0));if(event==eventHandles.end()){lastError=6;ret(0,1);return;}event->second->signaled=true;ret(1,1);}
void resetEvent(){auto event=eventHandles.find(arg(0));if(event==eventHandles.end()){lastError=6;ret(0,1);return;}event->second->signaled=false;ret(1,1);}
void* guestThreadEntry(void* context){
    auto& thread=*static_cast<GuestThread*>(context);
    std::unique_lock<MachineMutex> lease(machine);machineLease=&lease;guestThreadId=thread.id;
    State fresh;fresh.esp=thread.stack+(4u<<20)-64;fresh.fs=thread.tib;fresh.load();
    try {
        while(thread.suspended){checkGuestProgress();yieldGuestMachine(1);}
        if(thread.timer){
            runGuestTimers();
            thread.exitCode=0;
        }else thread.exitCode=callGuest(thread.start,{thread.parameter});
    }catch(const GuestStop& error){
        thread.exitCode=1;
        if(!stopRequested){guestThreadFault=error.what();NFS_RUNTIME_LOG(ANDROID_LOG_WARN,"NFSU2","Guest thread %u: %s",thread.id,error.what());}
    }catch(const std::exception& error){thread.exitCode=1;guestThreadFault=error.what();}
    heaps->free(GuestHeaps::process,thread.stack);heaps->free(GuestHeaps::process,thread.tib);
    for(auto& value:mutexHandles)if(value.second->owner==thread.id){value.second->owner=0;value.second->depth=0;value.second->abandoned=true;}
    thread.done=true;machineLease=nullptr;return nullptr;
}
void reapGuestThreads(){
    for(auto it=guestThreads.begin();it!=guestThreads.end();){
        auto& thread=*it;
        if(thread->done){if(thread->started){pthread_join(thread->native,nullptr);thread->started=false;}it=guestThreads.erase(it);}
        else ++it;
    }
}
void createThread(){
    reapGuestThreads();
    uint32_t flags=arg(4),idOut=arg(5);
    if(flags&~4u){lastError=87;ret(0,6);return;}
    auto thread=std::make_shared<GuestThread>();thread->id=nextThreadId++;thread->handle=nextThreadHandle++;
    thread->start=arg(2);thread->parameter=arg(3);thread->suspended=flags&4?1:0;
    if(!recomp_lookup(thread->start)){lastError=87;ret(0,6);return;}
    thread->stack=allocate(4u<<20);thread->tib=allocate(4096);
    write32(thread->tib,UINT32_MAX);write32(thread->tib+4,thread->stack+(4u<<20));write32(thread->tib+8,thread->stack);
    write32(thread->tib+0x18,thread->tib);write32(thread->tib+0x20,1);write32(thread->tib+0x24,thread->id);
    pthread_attr_t attributes;int error=pthread_attr_init(&attributes);
    if(!error){error=pthread_attr_setstacksize(&attributes,64u<<20);if(!error)error=pthread_create(&thread->native,&attributes,guestThreadEntry,thread.get());pthread_attr_destroy(&attributes);}
    if(error){heaps->free(GuestHeaps::process,thread->stack);heaps->free(GuestHeaps::process,thread->tib);lastError=8;ret(0,6);return;}
    thread->started=true;guestThreads.push_back(thread);threadHandles.emplace(thread->handle,thread);
    if(idOut)write32(idOut,thread->id);
    NFS_RUNTIME_LOG(ANDROID_LOG_INFO,"NFSU2","Created guest thread %u at %08x suspended=%u",thread->id,thread->start,thread->suspended);
    ret(thread->handle,6);
}
void resumeThread(){auto thread=threadHandles.find(arg(0));if(thread==threadHandles.end()){lastError=6;ret(UINT32_MAX,1);return;}uint32_t old=thread->second->suspended;if(old)--thread->second->suspended;ret(old,1);}
void setThreadPriority(){
    uint32_t handle=arg(0);int32_t priority=static_cast<int32_t>(arg(1));
    if(priority< -2&&priority!= -15){lastError=87;ret(0,2);return;}if(priority>2&&priority!=15){lastError=87;ret(0,2);return;}
    auto thread=threadHandles.find(handle);
    if(thread==threadHandles.end()){lastError=6;ret(0,2);return;}
    thread->second->priority=priority;ret(1,2);
}
void threadExitCode(){auto thread=threadHandles.find(arg(0));if(thread==threadHandles.end()){lastError=6;ret(0,2);return;}write32(arg(1),thread->second->exitCode);ret(1,2);}
void waitThread(){
    uint32_t handle=arg(0),timeout=arg(1);auto found=threadHandles.find(handle);
    auto mutexFound=mutexHandles.find(handle);
    if(mutexFound!=mutexHandles.end()){
        auto value=mutexFound->second;auto deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(timeout);
        while(value->owner&&value->owner!=guestThreadId){if(timeout!=UINT32_MAX&&std::chrono::steady_clock::now()>=deadline){ret(258,2);return;}checkGuestProgress();yieldGuestMachine(1);}
        value->owner=guestThreadId;++value->depth;bool abandoned=value->abandoned;value->abandoned=false;ret(abandoned?128:0,2);return;
    }
    auto eventFound=eventHandles.find(handle);
    if(found==threadHandles.end()&&eventFound==eventHandles.end()){lastError=6;ret(UINT32_MAX,2);return;}
    auto thread=found==threadHandles.end()?nullptr:found->second;
    auto event=eventFound==eventHandles.end()?nullptr:eventFound->second;
    auto deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(timeout);
    while(thread?!thread->done:!event->signaled){if(timeout!=UINT32_MAX&&std::chrono::steady_clock::now()>=deadline){ret(258,2);return;}checkGuestProgress();yieldGuestMachine(1);}
    if(event&&!event->manual)event->signaled=false;
    ret(0,2);
}
void joinGuestThreads(std::unique_lock<MachineMutex>& lease){
    stopRequested=true;lease.unlock();
    for(auto& thread:guestThreads)if(thread->started){pthread_join(thread->native,nullptr);thread->started=false;}
    lease.lock();
}
void runGuestTimers(){
    while(!stopRequested){
        checkGuestProgress();std::shared_ptr<GuestTimer> next;
        for(const auto& entry:timerHandles)if(!entry.second->cancelled&&(!next||entry.second->due<next->due))next=entry.second;
        if(!next||std::chrono::steady_clock::now()<next->due){yieldGuestMachine(1);continue;}
        next->running=true;runningTimerId=next->id;
        try{callGuest(next->callback,{next->id,0,next->parameter,0,0});}
        catch(...){next->running=false;runningTimerId=0;throw;}
        next->running=false;runningTimerId=0;
        if(next->periodic&&!next->cancelled){next->due=std::chrono::steady_clock::now()+std::chrono::milliseconds(next->delay);}
        else timerHandles.erase(next->id);
    }
}
void timeSetEvent(){
    uint32_t flags=arg(4),delay=arg(0),callback=arg(2);
    if(flags&~0x101u)throw GuestStop("timeSetEvent callback flags pending: "+hex(flags));
    if(!delay||(!recomp_lookup(callback)&&!recomp_lookup_manual(callback))){ret(0,5);return;}
    uint32_t id=nextTimerId++;auto timer=std::make_shared<GuestTimer>();
    timer->id=id;timer->delay=delay;timer->callback=callback;timer->parameter=arg(3);timer->periodic=flags&1;timer->synchronous=flags&0x100;
    timer->due=std::chrono::steady_clock::now()+std::chrono::milliseconds(delay);timerHandles.emplace(id,timer);
    if(timerWorker){if(timerWorker->done){timerHandles.erase(id);ret(0,5);return;}
        if(id%500==0)NFS_RUNTIME_LOG(ANDROID_LOG_INFO,"NFSU2","Guest timer %u reuses worker thread %u",id,timerWorker->id);ret(id,5);return;}
    auto thread=std::make_shared<GuestThread>();thread->id=nextThreadId++;thread->timer=true;
    thread->stack=allocate(4u<<20);thread->tib=allocate(4096);
    write32(thread->tib,UINT32_MAX);write32(thread->tib+4,thread->stack+(4u<<20));write32(thread->tib+8,thread->stack);
    write32(thread->tib+0x18,thread->tib);write32(thread->tib+0x20,1);write32(thread->tib+0x24,thread->id);
    pthread_attr_t attributes;int error=pthread_attr_init(&attributes);
    if(!error){error=pthread_attr_setstacksize(&attributes,64u<<20);if(!error)error=pthread_create(&thread->native,&attributes,guestThreadEntry,thread.get());pthread_attr_destroy(&attributes);}
    if(error){heaps->free(GuestHeaps::process,thread->stack);heaps->free(GuestHeaps::process,thread->tib);timerHandles.erase(id);ret(0,5);return;}
    thread->started=true;guestThreads.push_back(thread);timerWorker=thread;
    NFS_RUNTIME_LOG(ANDROID_LOG_INFO,"NFSU2","Created persistent guest timer worker thread %u",thread->id);ret(id,5);
}
void timeKillEvent(){
    auto timer=timerHandles.find(arg(0));if(timer==timerHandles.end()){ret(97,1);return;}
    auto value=timer->second;
    if(value->running&&runningTimerId==value->id&&value->synchronous)throw GuestStop("Synchronous timer self-cancellation pending");
    value->cancelled=true;
    if(value->synchronous)while(value->running){checkGuestProgress();yieldGuestMachine(1);}
    timerHandles.erase(value->id);ret(0,1);
}

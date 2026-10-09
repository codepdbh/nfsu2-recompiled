// DirectInput 8 ANSI ABI. Guest pointers and vtables remain 32-bit offsets.
constexpr uint32_t inputMethodBase=0x7d000000;
constexpr uint32_t diInvalid=0x80070057,diNotAcquired=0x8007000c;
std::mutex inputMutex;
std::array<uint8_t,256> inputKeys{};
std::atomic<bool> inputDrivingMode{false};
std::array<uint8_t,8> inputMouseButtons{};
int32_t inputMouseX{},inputMouseY{},inputMouseZ{};
struct InputFormatObject {uint32_t offset,type,bytes=1,user=UINT32_MAX;};
struct InputKeyEvent {uint64_t sequence;uint32_t scan,data,time;};
struct InputBufferedEvent {uint32_t offset,data,time,sequence,user;};
std::deque<InputKeyEvent> inputKeyEvents;uint64_t inputSequence=0;
void recordInputKey(unsigned scan,bool down){
    uint32_t time=uint32_t(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
    inputKeyEvents.push_back({++inputSequence,scan,down?0x80u:0u,time});if(inputKeyEvents.size()>4096)inputKeyEvents.pop_front();
}
struct InputObject {
    uint32_t kind,table,references=1,size=0,cooperative=0,bufferSize=0;
    bool acquired=false;
    std::vector<InputFormatObject> format;
    uint64_t sequence=0;bool overflow=false;
    std::deque<InputBufferedEvent> pending;
};
std::map<uint32_t,InputObject> inputObjects;
std::map<uint32_t,uint32_t> inputMethodCalls;
uint32_t inputToken{};
const std::array<uint8_t,16> keyboardGuid={0x61,0x2b,0x1d,0x6f,0xa0,0xd5,0xcf,0x11,0xbf,0xc7,0x44,0x45,0x53,0x54,0,0};
const std::array<uint8_t,16> mouseGuid={0x60,0x2b,0x1d,0x6f,0xa0,0xd5,0xcf,0x11,0xbf,0xc7,0x44,0x45,0x53,0x54,0,0};
const std::array<uint8_t,16> input8Guid={0x30,0x80,0x79,0xbf,0x3a,0x48,0xa2,0x4d,0xaa,0x99,0x5d,0x64,0xed,0x36,0x97,0};
const std::array<uint8_t,16> device8Guid={0x80,0x10,0xd4,0x54,0x15,0xdc,0x33,0x48,0xa4,0x1b,0x74,0x8f,0x73,0xa3,0x81,0x79};
bool inputGuidEquals(uint32_t address,const std::array<uint8_t,16>& guid){return address&&!std::memcmp(ptr(address,16),guid.data(),16);}
uint32_t createInputObject(uint32_t kind){
    uint32_t object=allocate(4),table=allocate(32*4);
    for(uint32_t slot=0;slot<32;++slot)write32(table+slot*4,inputMethodBase+kind*0x1000+slot*16);
    write32(object,table);inputObjects.emplace(object,InputObject{kind,table});return object;
}
void inputDeviceInstance(uint32_t address,uint32_t kind,uint32_t bytes){
    std::memset(ptr(address,bytes),0,bytes);write32(address,bytes);
    auto& guid=kind==2?keyboardGuid:mouseGuid;
    std::memcpy(ptr(address+4,16),guid.data(),16);std::memcpy(ptr(address+20,16),guid.data(),16);
    write32(address+36,kind==2?0x413:0x212);
    const char* name=kind==2?"Android keyboard":"Android pointer";
    std::memcpy(ptr(address+40,std::strlen(name)+1),name,std::strlen(name)+1);
    std::memcpy(ptr(address+300,std::strlen(name)+1),name,std::strlen(name)+1);
}
void directInputCreate(){
    uint32_t output=arg(3);if(!output){ret(0x80004003,5);return;}write32(output,0);
    if(arg(4)){ret(0x80040110,5);return;}
    if(arg(1)!=0x800){ret(diInvalid,5);return;}
    if(!inputGuidEquals(arg(2),input8Guid)){ret(0x80004002,5);return;}
    write32(output,createInputObject(1));ret(0,5);
}
bool validActionFormat(uint32_t format){
    if(!format||!memory->readable(format,328)||read32(format)!=328||read32(format+4)!=40)return false;
    uint32_t count=read32(format+12),actions=read32(format+16);
    return count&&count<=1024&&read32(format+8)==count*4&&actions&&memory->writable(actions,count*40ull);
}
unsigned buildKeyboardActions(uint32_t format){
    uint32_t count=read32(format+12),actions=read32(format+16);unsigned mapped=0;
    for(uint32_t i=0;i<count;++i){uint32_t action=actions+i*40,semantic=read32(action+4),flags=read32(action+8);
        if(flags&4)continue;
        if((flags&2)&&read32(action+36)){if(inputGuidEquals(action+16,keyboardGuid))++mapped;continue;}
        if((semantic&0xffffff00u)==0x81000400u){
            std::memcpy(ptr(action+16,16),keyboardGuid.data(),16);write32(action+32,((semantic&255)<<8)|4);write32(action+36,0x20);++mapped;
        }else if(inputGuidEquals(action+16,keyboardGuid))++mapped;
    }return mapped;
}
void releaseInputObject(uint32_t address){auto found=inputObjects.find(address);if(found==inputObjects.end())return;
    if(--found->second.references)return;heaps->free(GuestHeaps::process,found->second.table);heaps->free(GuestHeaps::process,address);inputObjects.erase(found);
}
void inputDispatch(){
    uint32_t kind=(inputToken-inputMethodBase)/0x1000,method=((inputToken-inputMethodBase)%0x1000)/16;
    uint32_t self=arg(0);auto found=inputObjects.find(self);
    if(found==inputObjects.end()||found->second.kind!=kind)throw GuestStop("Invalid DirectInput object");
    auto& object=found->second;
    if(++inputMethodCalls[inputToken]<=3)NFS_RUNTIME_LOG(ANDROID_LOG_INFO,"NFSU2","DirectInput kind=%u method=%u",kind,method);
    if(method==0){
        if(!arg(2)){ret(0x80004003,3);return;}write32(arg(2),0);
        std::array<uint8_t,16> unknown={0,0,0,0,0,0,0,0,0xc0,0,0,0,0,0,0,0x46};
        if(!inputGuidEquals(arg(1),kind==1?input8Guid:device8Guid)&&!inputGuidEquals(arg(1),unknown)){ret(0x80004002,3);return;}
        ++object.references;write32(arg(2),self);ret(0,3);return;
    }
    if(method==1){ret(++object.references,1);return;}
    if(method==2){uint32_t refs=--object.references;if(!refs){heaps->free(GuestHeaps::process,object.table);heaps->free(GuestHeaps::process,self);inputObjects.erase(found);}ret(refs,1);return;}
    if(kind==1){
        switch(method){
        case 3:{uint32_t output=arg(2);if(!output){ret(0x80004003,4);return;}write32(output,0);
            uint32_t device=inputGuidEquals(arg(1),keyboardGuid)?2:inputGuidEquals(arg(1),mouseGuid)?3:0;
            if(arg(3)){ret(0x80040110,4);return;}if(!device){ret(0x80070002,4);return;}
            write32(output,createInputObject(device));ret(0,4);return;}
        case 4:{uint32_t type=arg(1),callback=arg(2),context=arg(3),flags=arg(4);
            if(!callback||flags&~0x131u){ret(diInvalid,5);return;}
            uint32_t instance=allocate(580);
            try{for(uint32_t device:{2u,3u}){
                uint32_t deviceType=device==2?0x13:0x12;
                uint32_t deviceClass=device==2?3:2;
                if(type!=0&&type!=1&&type!=deviceClass&&(type&0xff)!=deviceType)continue;
                inputDeviceInstance(instance,device,580);
                if(!callGuest(callback,{instance,context}))break;
            }}catch(...){heaps->free(GuestHeaps::process,instance);throw;}
            heaps->free(GuestHeaps::process,instance);ret(0,5);return;}
        case 5:ret(inputGuidEquals(arg(1),keyboardGuid)||inputGuidEquals(arg(1),mouseGuid)?0:0x80070002,2);return;
        case 7:ret(arg(2)==0x800?0:diInvalid,3);return;
        case 9:{uint32_t format=arg(2),callback=arg(3),context=arg(4),flags=arg(5);
            if(!validActionFormat(format)||!callback||(flags&~0x7110u)){ret(diInvalid,6);return;}
            if(flags&(0x100u|0x2000u|0x4000u)){ret(0,6);return;} // No force-feedback, secondary or non-gaming HID devices.
            NFS_RUNTIME_LOG(ANDROID_LOG_INFO,"NFSU2","Enumerate action devices: %u actions callback=%08x",read32(format+12),callback);
            uint32_t instance=allocate(580);
            try{for(uint32_t kind:{2u,3u}){
                auto mapped=kind==2?buildKeyboardActions(format):0;uint32_t device=createInputObject(kind);
                // The semantic enumerator hands over a usable system device;
                // this guest does not call SetCooperativeLevel on that keyboard.
                inputObjects.at(device).cooperative=10;inputDeviceInstance(instance,kind,580);
                uint32_t proceed;try{proceed=callGuest(callback,{instance,device,0x20u|(mapped?1u:0u),kind==2?1u:0u,context});}catch(...){releaseInputObject(device);throw;}
                releaseInputObject(device);if(!proceed)break;
            }}catch(...){heaps->free(GuestHeaps::process,instance);throw;}
            heaps->free(GuestHeaps::process,instance);ret(0,6);return;}
        default:throw GuestStop("DirectInput interface method pending: "+std::to_string(method));
        }
    }
    switch(method){
    case 3:{uint32_t output=arg(1);if(!output||read32(output)!=44){ret(diInvalid,2);return;}
        std::memset(ptr(output+4,40),0,40);write32(output+4,1);write32(output+8,kind==2?0x413:0x212);
        write32(output+12,kind==2?0:3);write32(output+16,kind==2?256:8);ret(0,2);return;}
    case 5:case 6:{uint32_t property=arg(1),header=arg(2);
        if(!header||read32(header)!=20||read32(header+4)!=16||read32(header+12)!=0){ret(diInvalid,3);return;}
        if(property!=1){ret(0x80004001,3);return;}
        if(method==6){if(object.acquired){ret(0x800700aa,3);return;}if(read32(header+16)>4096){ret(diInvalid,3);return;}object.bufferSize=read32(header+16);}
        else write32(header+16,object.bufferSize);
        ret(0,3);return;}
    case 7:if(!object.size||!object.cooperative){ret(diInvalid,1);return;}if(object.acquired){ret(1,1);return;}
        {std::lock_guard<std::mutex> lock(inputMutex);object.sequence=inputSequence;object.pending.clear();object.overflow=false;}object.acquired=true;ret(0,1);return;
    case 8:{bool previous=object.acquired;object.acquired=false;ret(previous?0:1,1);return;}
    case 9:{if(!object.acquired){ret(diNotAcquired,3);return;}
        if(arg(1)!=object.size||!arg(2)){ret(diInvalid,3);return;}
        std::lock_guard<std::mutex> lock(inputMutex);std::memset(ptr(arg(2),object.size),0,object.size);
        for(auto item:object.format){uint32_t index=(item.type>>8)&0xffff,category=item.type&0xff;
            if(kind==2){if(item.bytes==4)write32(arg(2)+item.offset,inputKeys[index]);else *static_cast<uint8_t*>(ptr(arg(2)+item.offset,1))=inputKeys[index];}
            else if(category&3){int32_t value=index==0?inputMouseX:index==1?inputMouseY:inputMouseZ;write32(arg(2)+item.offset,static_cast<uint32_t>(value));}
            else *static_cast<uint8_t*>(ptr(arg(2)+item.offset,1))=inputMouseButtons[index];
        }
        if(kind==3)inputMouseX=inputMouseY=inputMouseZ=0;
        ret(0,3);return;}
    case 10:{uint32_t bytes=arg(1),out=arg(2),count=arg(3),flags=arg(4);
        if(!object.bufferSize){ret(0x80040207,5);return;}if(!object.acquired){ret(diNotAcquired,5);return;}
        if((bytes!=16&&bytes!=20)||!count||!memory->writable(count,4)||(flags&~1u)){ret(diInvalid,5);return;}
        if(kind!=2)throw GuestStop("Buffered mouse data pending");
        std::lock_guard<std::mutex> lock(inputMutex);
        if(!inputKeyEvents.empty()&&inputKeyEvents.front().sequence>object.sequence+1)object.overflow=true;
        for(auto event:inputKeyEvents){if(event.sequence<=object.sequence)continue;
            for(auto item:object.format){if(((item.type>>8)&0xffff)!=event.scan)continue;
                if(object.pending.size()>=object.bufferSize){object.pending.pop_front();object.overflow=true;}
                object.pending.push_back({item.offset,event.data,event.time,uint32_t(event.sequence),item.user});
            }
        }object.sequence=inputSequence;
        uint32_t amount=uint32_t(std::min<uint64_t>(read32(count),object.pending.size()));
        if(out&&!memory->writable(out,uint64_t(amount)*bytes)){ret(diInvalid,5);return;}
        if(out)for(uint32_t i=0;i<amount;++i)std::memcpy(ptr(out+i*bytes,bytes),&object.pending[i],bytes);
        uint32_t status=object.overflow?1:0;write32(count,amount);
        if(!(flags&1)){for(uint32_t i=0;i<amount;++i)object.pending.pop_front();object.overflow=false;}
        if(amount)NFS_RUNTIME_LOG(ANDROID_LOG_INFO,"NFSU2","Keyboard buffered input delivered %u events peek=%u from=%08x",amount,flags&1,g_cur_func);ret(status,5);return;}
    case 11:{uint32_t format=arg(1);if(object.acquired){ret(0x800700aa,2);return;}
        if(!format||read32(format)!=24||read32(format+4)!=16){ret(diInvalid,2);return;}
        uint32_t size=read32(format+12),count=read32(format+16),array=read32(format+20);
        if(!size||size>4096||count>256||(count&&!array)){ret(diInvalid,2);return;}
        std::vector<InputFormatObject> items;
        uint32_t nextAxis=0,nextButton=0;
        for(uint32_t i=0;i<count;++i){uint32_t offset=read32(array+i*16+4),type=read32(array+i*16+8),index=(type>>8)&0xffff,category=type&0xff;
            if(index==0xffff){index=(category&3)?nextAxis++:nextButton++;type=(type&~0xffff00u)|(index<<8);}
            uint32_t bytes=(category&3)?4:1;
            if(offset>size||bytes>size-offset||(kind==2&&(index>255||!(category&12)))||(kind==3&&((category&3)?index>2:index>7))){ret(diInvalid,2);return;}
            items.push_back({offset,type,bytes});}
        object.size=size;object.format=std::move(items);ret(0,2);return;}
    case 12:if(arg(1))throw GuestStop("DirectInput event notification pending");ret(0,2);return;
    case 13:{uint32_t flags=arg(2);if(!windows.count(arg(1))||flags&~31u||(!(flags&3))||((flags&3)==3)||(!(flags&12))||((flags&12)==12)){ret(diInvalid,3);return;}
        if((flags&9)==9){ret(0x80004001,3);return;}object.cooperative=flags;ret(0,3);return;}
    case 15:{uint32_t output=arg(1);if(!output||(read32(output)!=580&&read32(output)!=560)){ret(diInvalid,2);return;}inputDeviceInstance(output,kind,read32(output));ret(0,2);return;}
    case 25:ret(object.acquired?0:diNotAcquired,1);return;
    case 29:{if(!validActionFormat(arg(1))||(arg(3)!=0&&arg(3)!=1&&arg(3)!=2&&arg(3)!=4)){ret(diInvalid,4);return;}
        if(kind!=2){ret(0x80004001,4);return;}auto mapped=buildKeyboardActions(arg(1));
        NFS_RUNTIME_LOG(ANDROID_LOG_INFO,"NFSU2","Keyboard BuildActionMap mapped %u actions",mapped);ret(0,4);return;}
    case 30:{uint32_t format=arg(1);if(object.acquired){ret(0x800700aa,4);return;}
        if(!validActionFormat(format)||(arg(3)&~3u)){ret(diInvalid,4);return;}if(kind!=2){ret(0x80004001,4);return;}
        // DIDSAM_FORCESAVE (2) still installs the mapping. The game uses it
        // when switching from frontend keys to the driving action format.
        // Persistence stays in the guest profile; no host user database exists.
        std::vector<InputFormatObject> items;uint32_t actions=read32(format+16),count=read32(format+12);
        for(uint32_t i=0;i<count;++i){auto action=actions+i*40;if(!inputGuidEquals(action+16,keyboardGuid)||!read32(action+36))continue;
            auto type=read32(action+32);if(((type>>8)&0xffff)>255||!(type&4)){ret(diInvalid,4);return;}items.push_back({i*4,type,4,read32(action)});
            if(count>3)NFS_RUNTIME_LOG(ANDROID_LOG_INFO,"NFSU2","Keyboard action user=%u scan=%02x semantic=%08x",read32(action),(type>>8)&0xffff,read32(action+4));
        }
        if(read32(format+40)>4096){ret(diInvalid,4);return;}
        object.format=std::move(items);object.size=read32(format+8);object.bufferSize=read32(format+40);object.pending.clear();object.overflow=false;
        inputDrivingMode=std::any_of(object.format.begin(),object.format.end(),[](const InputFormatObject& item){return item.user==1&&((item.type>>8)&0xffff)==0xc8;});
        NFS_RUNTIME_LOG(ANDROID_LOG_INFO,"NFSU2","Keyboard SetActionMap installed %zu actions, %u bytes, buffer=%u",object.format.size(),object.size,object.bufferSize);ret(0,4);return;}
    default:throw GuestStop("DirectInput device method pending: "+std::to_string(method));
    }
}
void testGuestInput(){
    uint32_t keyboard=createInputObject(2),format=allocate(24),items=allocate(32),output=allocate(9);
    write32(format,24);write32(format+4,16);write32(format+8,2);write32(format+12,8);write32(format+16,2);write32(format+20,items);
    write32(items+4,1);write32(items+8,(0xc8<<8)|4);write32(items+20,7);write32(items+24,(0x1c<<8)|4);
    auto invoke=[&](uint32_t slot,std::vector<uint32_t> args){args.insert(args.begin(),keyboard);return callGuest(inputMethodBase+0x2000+slot*16,args);};
    if(invoke(11,{format})||invoke(9,{8,output})!=diNotAcquired)throw GuestStop("DirectInput format/acquisition check failed");
    inputObjects.at(keyboard).cooperative=6;
    if(invoke(7,{}))throw GuestStop("DirectInput acquisition failed");
    setGuestKey(0xc8,true);setGuestKey(0x1c,true);std::memset(ptr(output,9),0xcc,9);
    if(invoke(9,{8,output})||*static_cast<uint8_t*>(ptr(output+1,1))!=0x80||*static_cast<uint8_t*>(ptr(output+7,1))!=0x80||*static_cast<uint8_t*>(ptr(output+8,1))!=0xcc)throw GuestStop("DirectInput guest offset/guard check failed");
    setGuestKey(0xc8,false);if(invoke(9,{8,output})||*static_cast<uint8_t*>(ptr(output+1,1))!=0)throw GuestStop("DirectInput key release failed");
    clearGuestInput();invoke(8,{});if(invoke(9,{8,output})!=diNotAcquired)throw GuestStop("DirectInput unacquisition check failed");
    invoke(2,{});for(uint32_t address:{format,items,output})heaps->free(GuestHeaps::process,address);
    keyboard=createInputObject(2);format=allocate(328);items=allocate(120);output=allocate(80);uint32_t eventCount=allocate(4);
    write32(format,328);write32(format+4,40);write32(format+8,12);write32(format+12,3);write32(format+16,items);
    write32(items+4,0x81000401);write32(items+44,0x8100041c);write32(items+84,0x01000402);
    write32(format+40,2);write32(items,11);write32(items+40,22);
    if(invoke(29,{format,0,0})||!inputGuidEquals(items+16,keyboardGuid)||read32(items+32)!=0x104||read32(items+116))throw GuestStop("Keyboard semantic mapping failed");
    if(invoke(30,{format,0,0}))throw GuestStop("Keyboard action format failed");inputObjects.at(keyboard).cooperative=6;
    if(invoke(7,{}))throw GuestStop("Keyboard action acquisition failed");
    setGuestKey(1,true);setGuestKey(0x1c,true);write32(output+12,0xdeadbeef);
    if(invoke(9,{12,output})||read32(output)!=0x80||read32(output+4)!=0x80||read32(output+8)||read32(output+12)!=0xdeadbeef)throw GuestStop("Keyboard action state/guard failed");
    write32(eventCount,4);write32(output+40,0xdeadbeef);
    if(invoke(10,{20,output,eventCount,1})||read32(eventCount)!=2||read32(output)!=0||read32(output+4)!=0x80||read32(output+16)!=11||read32(output+20)!=4||read32(output+36)!=22||read32(output+40)!=0xdeadbeef)throw GuestStop("Keyboard buffered peek/data/guard failed");
    write32(eventCount,UINT32_MAX);if(invoke(10,{20,0,eventCount,0})||read32(eventCount)!=2)throw GuestStop("Keyboard buffered consumption failed");
    setGuestKey(1,false);if(invoke(9,{12,output})||read32(output))throw GuestStop("Keyboard action release failed");
    write32(eventCount,4);if(invoke(10,{20,output,eventCount,0})||read32(eventCount)!=1||read32(output+4))throw GuestStop("Keyboard buffered release failed");
    for(unsigned i=0;i<2;++i){setGuestKey(1,true);setGuestKey(1,false);}write32(eventCount,4);
    if(invoke(10,{20,output,eventCount,0})!=1||read32(eventCount)!=2||read32(output+4)!=0x80||read32(output+24))throw GuestStop("Keyboard buffered overflow/order failed");
    write32(eventCount,4);if(invoke(10,{20,output,eventCount,0})||read32(eventCount))throw GuestStop("Keyboard buffered drain failed");
    if(invoke(30,{format,0,2})!=0x800700aa)throw GuestStop("Keyboard acquired remapping guard failed");
    invoke(8,{});write32(items,3);write32(items+4,0x810004c8);
    std::memset(ptr(items+16,24),0,24);
    if(invoke(29,{format,0,2})||invoke(30,{format,0,2})||invoke(7,{}))throw GuestStop("Keyboard FORCESAVE action switch failed");
    setGuestKey(0xc8,true);write32(eventCount,4);
    if(invoke(10,{20,output,eventCount,0})||read32(eventCount)!=1||read32(output+4)!=0x80||read32(output+16)!=3)throw GuestStop("Keyboard driving remap press failed");
    setGuestKey(0xc8,false);write32(eventCount,4);
    if(invoke(10,{20,output,eventCount,0})||read32(eventCount)!=1||read32(output+4))throw GuestStop("Keyboard driving remap release failed");
    clearGuestInput();invoke(8,{});invoke(2,{});for(uint32_t address:{format,items,output,eventCount})heaps->free(GuestHeaps::process,address);
    {std::lock_guard<std::mutex> lock(messageMutex);windowMessages.clear();}
    NFS_RUNTIME_LOG(ANDROID_LOG_INFO,"NFSU2","DirectInput ABI check passed: offsets, guard, press/release, acquisition and stack cleanup");
    NFS_RUNTIME_LOG(ANDROID_LOG_INFO,"NFSU2","Keyboard action mapping passed: Escape/Enter DWORD state, unmapped action, guard and release");
    NFS_RUNTIME_LOG(ANDROID_LOG_INFO,"NFSU2","Keyboard buffered input passed: press/release, offsets, user data, peek, consume, overflow and guard");
    NFS_RUNTIME_LOG(ANDROID_LOG_INFO,"NFSU2","Keyboard action switch passed: acquired guard, FORCESAVE remap, driving press/release");
}

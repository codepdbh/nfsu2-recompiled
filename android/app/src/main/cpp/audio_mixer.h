#pragma once
#include <array>
#include <vector>
#include <memory>
#include <mutex>
#include <cstdint>
#include <cmath>
#include <algorithm>
struct PcmVoice {
    std::vector<uint8_t> samples;
    unsigned channels=2,bits=16,rate=48000,frequency=48000;
    double position=0;
    float left=1,right=1;
    bool playing=false,loop=false;
    unsigned block()const{return channels*(bits/8);}
};
class PcmMixer {
public:
    std::mutex mutex;
    std::array<std::shared_ptr<PcmVoice>,128> voices{};
    unsigned rate=48000;
    bool add(const std::shared_ptr<PcmVoice>& voice){
        std::lock_guard<std::mutex> lock(mutex);
        for(auto& slot:voices)if(!slot){slot=voice;return true;}return false;
    }
    void remove(const std::shared_ptr<PcmVoice>& voice){std::lock_guard<std::mutex> lock(mutex);for(auto& slot:voices)if(slot==voice)slot.reset();}
    static float sample(const PcmVoice& voice,size_t frame,unsigned channel){
        size_t offset=frame*voice.block()+std::min(channel,voice.channels-1)*(voice.bits/8);
        if(voice.bits==8)return (int(voice.samples[offset])-128)/128.f;
        uint16_t word=uint16_t(voice.samples[offset])|(uint16_t(voice.samples[offset+1])<<8);
        return static_cast<int16_t>(word)/32768.f;
    }
    void render(float* output,unsigned frames){
        std::fill(output,output+frames*2,0.f);
        // The guest holds this lock only for short copies of the regions it
        // unlocked and for cursor queries, so waiting is bounded. Skipping a
        // burst instead output silence and left every play cursor where it
        // was, so the game's view of what had played drifted from what had.
        std::lock_guard<std::mutex> lock(mutex);
        for(auto& voice:voices){
            if(!voice||!voice->playing||voice->samples.empty())continue;
            size_t count=voice->samples.size()/voice->block();if(!count)continue;
            double step=double(voice->frequency)/rate;
            for(unsigned frame=0;frame<frames;++frame){
                if(voice->position>=count){if(voice->loop)voice->position=std::fmod(voice->position,double(count));else{voice->playing=false;voice->position=0;break;}}
                size_t a=size_t(voice->position),b=a+1;float fraction=float(voice->position-a);
                if(b==count)b=voice->loop?0:a;
                for(unsigned channel=0;channel<2;++channel){float first=sample(*voice,a,channel),second=sample(*voice,b,channel);
                    output[frame*2+channel]+=(first+(second-first)*fraction)*(channel?voice->right:voice->left);}
                voice->position+=step;
            }
        }
        for(unsigned i=0;i<frames*2;++i)output[i]=std::clamp(output[i],-1.f,1.f);
    }
};

#pragma once
#include "HandpanEngine.h"
#include <algorithm>
#include <array>
#include <cstdint>

namespace handpan {
// Shared physical fields, not independent MIDI voices. Call on the audio thread.
class Performance {
public:
    static constexpr std::array<int,9> notes{50,57,58,60,62,64,65,69,72};
    double position=.25, hardness=.4, volume=.8;
    Technique technique=Technique::Fingerpad;
    bool dampOnRelease=false;
    unsigned releaseOnNextStrike=0;
    static int fieldFor(int note) {
        for(int f=0;f<9;++f) if(notes[f]==note) return f;
        return -1;
    }
    void dampAll(Engine& engine,double amount) {
        for(int f=0;f<Engine::fields;++f) engine.touch(f,amount);
    }
    bool midi(Engine& engine,std::uint32_t message) {
        const int status=message&0xf0, note=(message>>8)&127, value=(message>>16)&127;
        const int f=fieldFor(note);
        if(status==0x90 && value>0) {
            if(note>=24&&note<24+techniqueCount){technique=Technique(note-24);return true;}
            if(f<0) return false;
            if(releaseOnNextStrike&(1u<<f)){engine.touch(f,0);releaseOnNextStrike&=~(1u<<f);}
            return engine.strike(f,value/127.0,position,hardness,technique);
        }
        if(status==0x80 || (status==0x90 && value==0)) {
            if(f<0) return false;
            engine.touch(f,dampOnRelease?1:0);return true;
        }
        if(status==0xa0) {if(f<0)return false;engine.touch(f,value/127.0);return true;}
        if(status==0xd0) {dampAll(engine,note/127.0);return true;}
        if(status==0xc0&&note<techniqueCount){technique=Technique(note);return true;}
        if(status==0xb0) {
            if(note==1)position=value/127.0;
            else if(note==74)hardness=value/127.0;
            else if(note==7)volume=value/127.0;
            else if(note==120){engine.reset();releaseOnNextStrike=0;}
            else if(note==123){dampAll(engine,1);releaseOnNextStrike=511;}
            else if(note==121){position=.25;hardness=.4;volume=.8;technique=Technique::Fingerpad;dampAll(engine,0);releaseOnNextStrike=0;}
            else return false;
            return true;
        }
        return false;
    }
};
}


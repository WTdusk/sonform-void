#pragma once
#include "SoundControls.h"


#include <map>

using FxValues=std::map<std::string,float>;
inline FxValues fxDefaults(){FxValues v;for(auto& c:soundControls())v[c.id]=c.initial;v["delaySync"]=0;v["delayDivision"]=3;return v;}
inline constexpr std::array<const char*,6> fxPowerIDs{"gainEqEnabled","eqEnabled","compressEnabled","grainEnabled","delayEnabled","reverbEnabled"};
inline void selectFx(FxValues& values,int scenario){for(size_t i=0;i<fxPowerIDs.size();++i)values[fxPowerIDs[i]]=scenario==7||scenario==int(i)+1?1.f:0.f;}
inline std::array<double,2> fxInput(int sample,double rate){double t=sample/rate;return {.11*std::sin(t*2764.601535)+.037*std::sin(t*11303.45)+(sample%7919==0?.23:0),.09*std::sin(t*1935.221074)+.024*std::sin(t*8701.32)};}
inline void checkFxBehaviour(){
    auto check=[](bool ok,const char* msg){if(!ok)throw std::runtime_error(msg);};
    for(int scenario=1;scenario<=6;++scenario){
        auto values=fxDefaults();selectFx(values,scenario);values["delayTime"]=20;values["delaySend"]=values["reverbSend"]=values["grainMix"]=100;values["gainLow"]=6;values["eqLow"]=6;
        auto read=[&](const char* id){return values.at(id);};StudioEffects fx;fx.prepare(48000);fx.configure(read,120,true);
        for(int n=0;n<16000;++n)fx.tick(fxInput(n,48000));
        values[fxPowerIDs[size_t(scenario-1)]]=0;fx.configure(read,120);for(int n=0;n<2000;++n)fx.tick({0,0});check(fx.tick({.123456789,-.23456789})==std::array<double,2>{.123456789,-.23456789},"Sleeping FX changed dry signal");
        values[fxPowerIDs[size_t(scenario-1)]]=1;fx.configure(read,120);for(int n=0;n<24000;++n)check(fx.tick({0,0})==std::array<double,2>{},"Reactivated FX replayed an old tail or recording");
    }
    {
        auto values=fxDefaults();selectFx(values,4);values["grainMix"]=100;auto read=[&](const char* id){return values.at(id);};StudioEffects fx;fx.prepare(48000);fx.configure(read,120,true);
        for(int n=0;n<48000;++n)fx.tick(fxInput(n,48000));values["grainFreeze"]=1;fx.configure(read,120);for(int n=0;n<5000;++n)fx.tick({0,0});values["grainEnabled"]=0;fx.configure(read,120);for(int n=0;n<5000;++n)fx.tick({0,0});
        values["grainEnabled"]=1;fx.configure(read,120);double energy=0;for(int n=0;n<24000;++n){auto s=fx.tick({0,0});energy+=s[0]*s[0]+s[1]*s[1];}check(energy>.01,"Bypass discarded frozen granular audio");
    }
    {
        auto values=fxDefaults();auto read=[&](const char* id){return values.at(id);};StudioEffects fx;fx.prepare(48000);fx.configure(read,120,true);double previous=.1;
        for(int block=0;block<100;++block){selectFx(values,block%2?0:7);fx.configure(read,120);for(int n=0;n<37;++n){auto s=fx.tick({.1,.1});check(std::isfinite(s[0])&&std::abs(s[0]-previous)<.04,"Rapid FX toggles caused an abrupt gain jump");previous=s[0];}}
    }
}


#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "GranularEffect.h"
#include "StereoCompressor.h"
#include <unordered_map>
#include <string_view>

struct SoundControl {
    const char* id;const char* name;float low,high,initial,skew;const char* unit;
};
inline const std::array<SoundControl,40>& soundControls(){
    static const std::array<SoundControl,40> controls{{
        {"attack","Attack",0,500,0,.4f,"ms"},
        {"decay","Decay",.25f,2,1,1,"x"},
        {"damping","Damp",0,100,0,1,"%"},
        {"lowCut","Low cut",20,500,20,.5f,"Hz"},
        {"eqLow","Low shelf",-12,12,0,1,"dB"},
        {"eqHigh","High shelf",-12,12,0,1,"dB"},
        {"delaySend","Delay mix",0,100,25,1,"%"},
        {"delayTime","Time",20,2000,375,.5f,"ms"},
        {"delayFeedback","Feedback",0,85,35,1,"%"},
        {"delayTone","High cut",600,16000,6500,.45f,"Hz"},
        {"reverbSend","Reverb mix",0,100,20,1,"%"},
        {"reverbPredelay","Pre-delay",0,150,20,.6f,"ms"},
        {"reverbSize","Size / decay",0,100,55,1,"%"},
        {"reverbTone","Damping",0,100,45,1,"%"},
        {"reverbWidth","Width",0,100,100,1,"%"},
        {"envEnabled","Envelope",0,1,1,1,"bool"},
        {"eqEnabled","Filter EQ",0,1,0,1,"bool"},
        {"delayEnabled","Delay",0,1,0,1,"bool"},
        {"reverbEnabled","Reverb",0,1,0,1,"bool"},
        {"gainLow","Low / 150 Hz",-12,12,0,1,"dB"},
        {"gainMid","Mid / 1 kHz",-12,12,0,1,"dB"},
        {"gainHigh","High / 6 kHz",-12,12,0,1,"dB"},
        {"gainEqEnabled","Gain EQ",0,1,0,1,"bool"},
        {"highCut","High cut",500,20000,20000,.5f,"Hz"},
        {"grainMix","Grain mix",0,100,35,1,"%"},
        {"grainSize","Grain size",20,400,100,.6f,"ms"},
        {"grainDensity","Density",1,40,12,.6f,"/s"},
        {"grainPitch","Pitch",-24,24,0,1,"st"},
        {"grainPosition","Position",20,1500,150,.5f,"ms"},
        {"grainSpray","Spray",0,100,30,1,"%"},
        {"grainEnabled","Granular",0,1,0,1,"bool"},
        {"grainReverse","Reverse",0,1,0,1,"bool"},
        {"grainFreeze","Freeze",0,1,0,1,"bool"},
        {"compressEnabled","Compress",0,1,0,1,"bool"},
        {"compressThreshold","Threshold",-60,0,-18,1,"dB"},
        {"compressRatio","Ratio",1,10,3,.6f,":1"},
        {"compressAttack","Attack",.1f,100,10,.4f,"ms"},
        {"compressRelease","Release",10,1000,100,.5f,"ms"},
        {"compressMakeup","Makeup",0,24,0,1,"dB"},
        {"compressMix","Mix",0,100,100,1,"%"}
    }};return controls;
}
inline const SoundControl& soundControl(const juce::String& id){for(auto& c:soundControls())if(id==c.id)return c; jassertfalse;return soundControls()[0];}
inline void addSoundParameters(juce::AudioProcessorValueTreeState::ParameterLayout& layout){
    auto add=[&](const SoundControl& c){
        if(juce::String(c.unit)=="bool")layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{c.id,1},c.name,c.initial>.5f));
        else layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{c.id,1},c.name,juce::NormalisableRange<float>(c.low,c.high,0,c.skew),c.initial));
    };
    // Existing FX IDs are stable; removed strike controls are no longer exposed.
    for(size_t i=0;i<19;++i)add(soundControls()[i]);
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"delaySync",1},"Tempo sync",true));
    layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{"delayDivision",1},"Delay division",juce::StringArray{"1/16","1/8","1/8 dotted","1/4","1/4 dotted","1/2","1/1"},2));
    for(size_t i=19;i<soundControls().size();++i)add(soundControls()[i]);
}
inline void initialiseEffects(juce::AudioProcessorValueTreeState& state){
    auto reset=[&](const char* id){auto* p=state.getParameter(id);p->beginChangeGesture();p->setValueNotifyingHost(p->getDefaultValue());p->endChangeGesture();};
    for(auto& c:soundControls())if(juce::String(c.id)!="attack"&&juce::String(c.id)!="decay"&&juce::String(c.id)!="damping"&&juce::String(c.id)!="envEnabled")reset(c.id);
    reset("delaySync");reset("delayDivision");
}

// All audio storage is allocated before playback. Mix is a real dry/wet blend;
// a bypassed module always returns its input while preserving its knob values.
class StudioEffects {
    using Smooth=juce::SmoothedValue<float>;
    double rate=48000;
    std::array<juce::IIRFilter,2> cut,topCut,low,high,gainLow,gainMid,gainHigh;
    GranularEffect granular;
    StereoCompressor compressor;
    juce::Reverb reverb;
    std::array<std::vector<float>,2> delay,predelay;
    size_t cursor=0,preCursor=0,delayValid=0,preValid=0;
    enum Module:unsigned { gainEQ=1,filterEQ=2,compress=4,grain=8,echo=16,room=32 };
    unsigned processingMask=0,awakeMask=0;
    std::array<float,2> feedbackState{};
    Smooth compressPower,compressMix,compressMakeup,eqMix,gainEqMix,grainPower,grainLevel,delayMix,reverbMix,delayLevel,reverbLevel,feedback,delaySamples,preSamples,tone;
    float lastCut=-1,lastTop=-1,lastLow=100,lastHigh=100;
    std::array<float,3> lastGain{100,100,100};
    float lastMakeup=-1,lastTone=-1;std::array<float,3> lastReverb{-1,-1,-1};
    const juce::AudioProcessorValueTreeState* boundState=nullptr;
    std::unordered_map<std::string_view,std::atomic<float>*> parameterPointers;
    static float read(const std::vector<float>& data,size_t position,float offset,size_t valid){
        if(double(offset)>=double(valid)+1)return 0;
        double location=double(position)-offset;if(location<0)location+=data.size();
        const size_t a=size_t(location),b=a+1==data.size()?0:a+1;float f=float(location-a),older=offset>valid?0:data[a];return older+f*(data[b]-older);
    }
    void wake(unsigned module){
        if(awakeMask&module)return;awakeMask|=module;
        switch(module){
            case gainEQ:for(auto* filters:{&gainLow,&gainMid,&gainHigh})for(auto& f:*filters)f.reset();break;
            case filterEQ:for(auto* filters:{&cut,&topCut,&low,&high})for(auto& f:*filters)f.reset();break;
            case compress:compressor.reset();break;
            case grain:granular.resume();break;
            case echo:cursor=delayValid=0;feedbackState={};break;
            case room:preCursor=preValid=0;reverb.reset();break;
        }
    }
    void sleep(unsigned module){if((awakeMask&module)&&module==grain)granular.suspend();awakeMask&=~module;}
    float powerStep(Smooth& power,unsigned module){float amount=power.getNextValue();if(amount>0)wake(module);else{sleep(module);processingMask&=~module;}return amount;}
    static bool active(const Smooth& power){return power.getCurrentValue()>0||power.getTargetValue()>0;}
    static void settle(Smooth& value){value.setCurrentAndTargetValue(value.getTargetValue());}
    static void target(Smooth& s,float value,bool immediate){if(immediate)s.setCurrentAndTargetValue(value);else s.setTargetValue(value);}
public:
    // Bind once during processor construction, never allocate or build parameter
    // strings in processBlock. APVTS keeps these atomics stable on state restore.
    void bind(juce::AudioProcessorValueTreeState& state){parameterPointers.clear();parameterPointers.reserve(48);for(auto& c:soundControls())parameterPointers.emplace(c.id,state.getRawParameterValue(c.id));for(const char* id:{"delaySync","delayDivision"})parameterPointers.emplace(id,state.getRawParameterValue(id));boundState=&state;}
    void prepare(double sampleRate){
        rate=sampleRate;for(auto& channel:delay)channel.assign(size_t(rate*12.1)+4,0);for(auto& channel:predelay)channel.assign(size_t(rate*.16)+4,0);
        for(auto* s:{&compressPower,&compressMix,&compressMakeup,&eqMix,&gainEqMix,&grainPower,&grainLevel,&delayMix,&reverbMix,&delayLevel,&reverbLevel,&feedback,&preSamples,&tone})s->reset(rate,.03);
        delaySamples.reset(rate,.15);reverb.setSampleRate(rate);lastCut=lastTop=-1;lastLow=lastHigh=100;lastGain.fill(100);lastMakeup=lastTone=-1;lastReverb.fill(-1);granular.prepare(rate);compressor.prepare(rate);reset();
    }
    void reset(){cursor=preCursor=delayValid=preValid=0;awakeMask=0;feedbackState={};granular.reset();compressor.reset();for(auto* filters:{&cut,&topCut,&low,&high,&gainLow,&gainMid,&gainHigh})for(auto& f:*filters)f.reset();}
    void update(juce::AudioProcessorValueTreeState& state,double bpm,bool immediate=false){jassert(boundState==&state);configure([&](const char* id){auto found=parameterPointers.find(id);jassert(found!=parameterPointers.end()&&found->second!=nullptr);return found->second->load(std::memory_order_relaxed);},bpm,immediate);}
    bool silent()const{return processingMask==0;}
    float gainReduction()const{return compressor.gainReduction();}
    template<class Read> void configure(Read value,double bpm,bool immediate=false){
        compressor.configure(value("compressThreshold"),value("compressRatio"),value("compressAttack"),value("compressRelease"),value("compressEnabled")>.5f);
        target(compressPower,value("compressEnabled"),immediate);target(compressMix,value("compressMix")*.01f,immediate);const float makeupDb=value("compressMakeup");if(immediate||makeupDb!=lastMakeup){target(compressMakeup,juce::Decibels::decibelsToGain(makeupDb),immediate);lastMakeup=makeupDb;}
        const float lc=value("lowCut"),hc=std::min(value("highCut"),float(rate*.45)),lo=value("eqLow"),hi=value("eqHigh");
        if(hc!=lastTop){for(auto& f:topCut)f.setCoefficients(juce::IIRCoefficients::makeLowPass(rate,hc));lastTop=hc;}
        std::array<float,3> gains{value("gainLow"),value("gainMid"),value("gainHigh")};std::array<double,3> frequencies{150,1000,6000};
        std::array<std::array<juce::IIRFilter,2>*,3> bands{&gainLow,&gainMid,&gainHigh};
        for(size_t b=0;b<3;++b)if(gains[b]!=lastGain[b]){for(auto& f:*bands[b])f.setCoefficients(juce::IIRCoefficients::makePeakFilter(rate,frequencies[b],.707,juce::Decibels::decibelsToGain(gains[b])));lastGain[b]=gains[b];}
        if(lc!=lastCut){for(auto& f:cut)f.setCoefficients(juce::IIRCoefficients::makeHighPass(rate,lc));lastCut=lc;}
        if(lo!=lastLow){for(auto& f:low)f.setCoefficients(juce::IIRCoefficients::makeLowShelf(rate,220,.707,juce::Decibels::decibelsToGain(lo)));lastLow=lo;}
        if(hi!=lastHigh){for(auto& f:high)f.setCoefficients(juce::IIRCoefficients::makeHighShelf(rate,3500,.707,juce::Decibels::decibelsToGain(hi)));lastHigh=hi;}
        target(eqMix,value("eqEnabled"),immediate);target(gainEqMix,value("gainEqEnabled"),immediate);target(grainPower,value("grainEnabled"),immediate);target(grainLevel,value("grainMix")*.01f,immediate);
        granular.configure(value("grainEnabled")>.5f,value("grainSize"),value("grainDensity"),value("grainPitch"),value("grainPosition"),value("grainSpray"),value("grainReverse")>.5f,value("grainFreeze")>.5f);target(delayMix,value("delayEnabled"),immediate);target(reverbMix,value("reverbEnabled"),immediate);
        target(delayLevel,value("delaySend")*.01f,immediate);target(reverbLevel,value("reverbSend")*.01f,immediate);target(feedback,value("delayFeedback")*.01f,immediate);
        static constexpr std::array<double,7> beats{.25,.5,.75,1,1.5,2,4};
        double seconds=value("delaySync")>.5f?60/std::clamp(bpm,20.,400.)*beats[size_t(std::clamp(int(value("delayDivision")),0,6))]:value("delayTime")*.001;
        target(delaySamples,float(std::clamp(seconds,.02,12.)*rate),immediate);target(preSamples,value("reverbPredelay")*.001f*float(rate),immediate);
        const float cutoff=value("delayTone");if(immediate||cutoff!=lastTone){target(tone,float(1-std::exp(-juce::MathConstants<double>::twoPi*cutoff/rate)),immediate);lastTone=cutoff;}
        std::array<float,3> roomSettings{value("reverbSize"),value("reverbTone"),value("reverbWidth")};
        if(roomSettings!=lastReverb){juce::Reverb::Parameters settings;settings.roomSize=roomSettings[0]*.009f;settings.damping=roomSettings[1]*.01f;settings.width=roomSettings[2]*.01f;settings.wetLevel=1;settings.dryLevel=0;settings.freezeMode=0;reverb.setParameters(settings);lastReverb=roomSettings;}
        processingMask=(active(gainEqMix)?gainEQ:0u)|(active(eqMix)?filterEQ:0u)|(active(compressPower)?compress:0u)|(active(grainPower)?grain:0u)|(active(delayMix)?echo:0u)|(active(reverbMix)?room:0u);
        for(unsigned module:{gainEQ,filterEQ,compress,grain,echo,room})if(!(processingMask&module))sleep(module);
        if(!(processingMask&compress)){settle(compressMix);settle(compressMakeup);}
        if(!(processingMask&grain))settle(grainLevel);
        if(!(processingMask&echo)){settle(delayLevel);settle(feedback);settle(delaySamples);settle(tone);}
        if(!(processingMask&room)){settle(reverbLevel);settle(preSamples);}
    }
    std::array<double,2> tick(std::array<double,2> input){
        if(processingMask==0)return input;
        auto dry=input;
        if(processingMask&gainEQ)if(float amount=powerStep(gainEqMix,gainEQ);amount>0)for(size_t c=0;c<2;++c){float filtered=gainHigh[c].processSingleSampleRaw(gainMid[c].processSingleSampleRaw(gainLow[c].processSingleSampleRaw(float(dry[c]))));dry[c]+=amount*(filtered-dry[c]);}
        if(processingMask&filterEQ)if(float amount=powerStep(eqMix,filterEQ);amount>0)for(size_t c=0;c<2;++c){float filtered=high[c].processSingleSampleRaw(low[c].processSingleSampleRaw(topCut[c].processSingleSampleRaw(cut[c].processSingleSampleRaw(float(dry[c])))));dry[c]+=amount*(filtered-dry[c]);}
        if(processingMask&compress)if(float power=powerStep(compressPower,compress);power>0){auto wet=compressor.tick(dry);float mix=power*compressMix.getNextValue(),makeup=compressMakeup.getNextValue();for(size_t c=0;c<2;++c)dry[c]+=mix*(wet[c]*makeup-dry[c]);}
        if(processingMask&grain)if(float power=powerStep(grainPower,grain);power>0){auto wet=granular.tick(dry);float mix=power*grainLevel.getNextValue();for(size_t c=0;c<2;++c)dry[c]+=mix*(wet[c]-dry[c]);}
        if(processingMask&echo)if(float power=powerStep(delayMix,echo);power>0){
            float mix=delayLevel.getNextValue(),fb=feedback.getNextValue(),time=delaySamples.getNextValue(),lp=tone.getNextValue();
            for(size_t c=0;c<2;++c){float wet=read(delay[c],cursor,time,delayValid);feedbackState[c]+=lp*(wet-feedbackState[c]);delay[c][cursor]=power*(float(dry[c])+feedbackState[c]*fb);dry[c]+=power*mix*(wet-dry[c]);}
            if(++cursor==delay[0].size())cursor=0;delayValid=std::min(delayValid+1,delay[0].size()-1);
        }
        if(processingMask&room)if(float power=powerStep(reverbMix,room);power>0){
            float mix=reverbLevel.getNextValue(),pre=preSamples.getNextValue();std::array<float,2> wet{};
            preValid=std::min(preValid+1,predelay[0].size());
            for(size_t c=0;c<2;++c){predelay[c][preCursor]=float(dry[c])*power;wet[c]=read(predelay[c],preCursor,pre,preValid-1);}
            if(++preCursor==predelay[0].size())preCursor=0;reverb.processStereo(&wet[0],&wet[1],1);for(size_t c=0;c<2;++c)dry[c]+=power*mix*(wet[c]-dry[c]);
        }
        return dry;
    }
};

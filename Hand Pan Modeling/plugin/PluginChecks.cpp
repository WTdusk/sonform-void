#include <juce_audio_utils/juce_audio_utils.h>
#include <iostream>
#include "ModelRegistry.h"
#include "BinaryData.h"
#include "PanKeyMap.h"
#include "../src/HandpanEngine.h"
#include "../src/StrikeTexture.h"
#include "../src/StrikeTail.h"
#include "../src/ModalBankEngine.h"
#include "SoundControls.h"
#include <map>
#include "FxChecks.h"
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();
void require(bool condition,const char* msg){if(!condition)throw std::runtime_error(msg);}
juce::Component* findComponent(juce::Component& root,const juce::String& id){if(root.getComponentID()==id)return &root;for(auto* child:root.getChildren())if(auto* match=findComponent(*child,id))return match;return nullptr;}
int main(){try{
    juce::ScopedJuceInitialiser_GUI initialise;
    checkFxBehaviour();
    {
        // 0.9.3 saved only the parameter tree. Freeze had no corresponding recording.
        auto legacy=juce::parseXML(R"(<SonformVoidState>
            <PARAM id="model" value="4"/><PARAM id="articulation" value="1"/>
            <PARAM id="grainFreeze" value="1"/>
        </SonformVoidState>)");
        std::unique_ptr<juce::AudioProcessor> processor(createPluginFilter());
        auto restore=[&](const juce::XmlElement& xml){
            juce::MemoryBlock blob;
            juce::AudioProcessor::copyXmlToBinary(xml,blob);
            processor->setStateInformation(blob.getData(),int(blob.getSize()));
        };
        restore(*legacy);
        const auto& parameters=processor->getParameters();
        require(std::abs(parameters[0]->getValue()-4.f/6)<1e-6,"Legacy model was not restored");
        require(std::abs(parameters[1]->getValue()-1.f/21)<1e-6,"Legacy articulation was not restored");
        for(auto* parameter:parameters)
            if(auto* identified=dynamic_cast<juce::AudioProcessorParameterWithID*>(parameter);
               identified&&identified->paramID=="grainFreeze")
                require(parameter->getValue()==0,"Legacy freeze restored an empty recording");
        legacy->setAttribute("schemaVersion",1);
        legacy->setAttribute("modelId","somera");
        legacy->setAttribute("articulationGroup",4);
        restore(*legacy);
        require(parameters[0]->getValue()==1,"Stable model ID did not override stale index");
        require(std::abs(parameters[1]->getValue()-2.f/21)<1e-6,"Stable articulation ID did not override stale index");
    }
    {
        // Change instruments twice inside one fade and compare the transition sample.
        std::unique_ptr<juce::AudioProcessor> rapid(createPluginFilter()),reference(createPluginFilter());
        juce::AudioBuffer<float> actual(2,64),expected(2,64);
        for(auto* processor:{rapid.get(),reference.get()})processor->prepareToPlay(48000,64);
        for(auto* processor:{rapid.get(),reference.get()}){
            juce::MidiBuffer note;
            note.addEvent(juce::MidiMessage::noteOn(1,65,juce::uint8(100)),0);
            processor->processBlock(actual,note);
            processor->getParameters()[0]->setValueNotifyingHost(1.f/6);
            processor->processBlock(actual,note);
        }
        rapid->getParameters()[0]->setValueNotifyingHost(2.f/6);
        juce::MidiBuffer empty;
        rapid->processBlock(actual,empty);
        reference->processBlock(expected,empty);
        require(std::abs(expected.getSample(0,0))>1e-6f,"Fade test requires an audible source tail");
        require(std::abs(actual.getSample(0,0)-expected.getSample(0,0))<1e-7f,"Rapid switch discontinuity");
    }

    {
        const std::array<int,27> expected{21,21,21,21,21,21,24,24,26,26,28,28,29,31,31,33,33,33,36,36,36,36,36,36,36,36,36};
        for(int key=17;key<=43;++key)require(handpan::cornerModelKey(key)==expected[size_t(key-17)],"Serevin Corner source-zone selection");
        for(int key:{0,16,44,127})require(handpan::cornerModelKey(key)==key,"Serevin Corner outside fallback");
        handpan::ModalBankEngine probe;std::istringstream bank("family,group,root,velocity,mode,frequency,loss,left,right\nserevin,82,24,127,0,300,1,0.01,0.01\nserevin,82,26,127,0,440,1,0.02,0.02\nserevin,80,24,127,0,300,1,0.01,0.01\nserevin,80,26,127,0,440,1,0.02,0.02\n");probe.load(bank);
        for(int group:{80,82}){probe.prepare(48000);probe.strike("serevin",group,25,1);double f=(group==82?440:300)*std::pow(2.,(group==82?-1:1)/12.);double amp=group==82?.02:.01;
            for(int i=0;i<4096;++i){auto x=probe.tick();double expected=-amp*std::sin(6.283185307179586*f*i/48000)*std::exp(-i/48000.);require(std::abs(x[0]-expected)<1e-12,"Serevin source-zone frequency and amplitude");}
        }
    }
    std::unique_ptr<juce::AudioProcessor> p(createPluginFilter());
    {
        const std::string csv="family,group,root,velocity,mode,frequency,loss,left,right\nprobe,0,60,32,0,440,3,0.8,0.8\nprobe,0,60,127,0,440,3,3.2,3.2\n";
        auto load=[&](handpan::ModalBankEngine& e){std::istringstream input(csv);e.load(input);e.prepare(48000);};
        handpan::ModalBankEngine a,b;load(a);load(b);
        a.strike("probe",0,60,32./127);b.strike("probe",0,60,1);double soft=0,loud=0;
        for(int n=0;n<2048;++n){auto x=a.tick(),y=b.tick();soft+=x[0]*x[0];loud+=y[0]*y[0];}require(loud>soft*8,"Per-hit limiting flattened velocity dynamics");
        load(a);load(b);for(int i=0;i<48;++i){a.strike("probe",0,60,.5);b.strike("probe",0,60,.5);}for(int n=0;n<2000;++n){a.tick();b.tick();}
        a.strike("probe",0,60,.5);auto stolen=a.tick(),expected=b.tick();require(std::abs(stolen[0]-expected[0])<1e-10,"Voice steal discontinuity");
        load(a);load(b);a.strike("probe",0,60,.5);b.strike("probe",0,60,.5);for(int n=0;n<100;++n){a.tick();b.tick();}a.release();require(std::abs(a.tick()[0]-b.tick()[0])<1e-12,"Damp started with an abrupt gain jump");for(int n=0;n<1024;++n)a.tick();require(a.tick()==std::array<double,2>{},"Smooth modal damp did not finish");
    }
    require(p->getName()=="SONFORM: Void","Product name mismatch");
    auto params=p->getParameters();require(params.size()==45,"Parameter contract");
    for(int i=0;i<3;++i){const std::array<const char*,3> ids{"model","articulation","gain"};require(dynamic_cast<juce::AudioProcessorParameterWithID*>(params[i])->paramID==ids[size_t(i)],"Original host parameter identity changed");}
    auto parameter=[&](const char* id)->juce::RangedAudioParameter*{for(auto* param:params)if(auto* ranged=dynamic_cast<juce::RangedAudioParameter*>(param))if(ranged->getParameterID()==id)return ranged;throw std::runtime_error("Missing parameter");};
    auto set=[&](const char* id,float value){auto* par=parameter(id);par->setValueNotifyingHost(par->convertTo0to1(value));};
    require(std::abs(params[2]->getDefaultValue()-.85f)<1e-6,"Output default not raised");
    for(auto* p:params)if(auto* id=dynamic_cast<juce::AudioProcessorParameterWithID*>(p))require(id->paramID!="position"&&id->paramID!="hardness","Removed strike controls still exposed to host");
    for(const char* id:{"eqEnabled","gainEqEnabled","delayEnabled","reverbEnabled","grainEnabled","compressEnabled"})require(parameter(id)->getDefaultValue()==0&&parameter(id)->getValue()==0,"FX is not initially bypassed");
    {
        std::map<std::string,float> values;for(auto& c:soundControls())values[c.id]=c.initial;values["delaySync"]=0;values["delayDivision"]=3;
        auto read=[&](const char* id){return values.at(id);};StudioEffects fx;
        for(double rate:{32000.,48000.,96000.}){
            fx.prepare(rate);fx.configure(read,120,true);
            for(int n=0;n<1000;++n){std::array<double,2> input{n==0?.123456789:0,n==10?-.23:0};require(fx.tick(input)==input,"Default FX changed dry signal");}
            values["delayEnabled"]=1;values["delaySend"]=100;values["delayFeedback"]=0;values["delayTime"]=100;
            fx.prepare(rate);fx.configure(read,120,true);double before=0,arrival=0;
            for(int n=0;n<int(rate*.11);++n){auto x=fx.tick({n==0?1.:0,0});if(n>0&&n<int(rate*.1)-1)before+=std::abs(x[0]);if(std::abs(n-int(rate*.1))<=1)arrival+=x[0];}
            require(before==0&&arrival>.999,"Delay time or send routing incorrect");
            fx.reset();for(int n=0;n<1000;++n)require(fx.tick({0,0})==std::array<double,2>{},"FX reset retained tail");
            values["delaySend"]=0;values["delayEnabled"]=0;
        }
        fx.prepare(48000);values["delaySync"]=1;values["delayDivision"]=3;values["delayEnabled"]=1;values["delaySend"]=100;fx.configure(read,60,true);
        double beat=0;for(int n=0;n<48002;++n){auto x=fx.tick({n==0?1.:0,0});if(n>=47999)beat+=x[0];else if(n>0)require(x[0]==0,"Tempo sync echoed before one beat");}require(beat>.999,"Host tempo did not control delay");
        values["delayEnabled"]=0;values["delaySend"]=0;values["reverbEnabled"]=1;values["reverbSend"]=100;fx.prepare(48000);fx.configure(read,120,true);double tail=0;
        for(int n=0;n<48000;++n){auto x=fx.tick({n==0?1.:0,0});for(double s:x)require(std::isfinite(s),"Reverb nonfinite");if(n>12000)tail+=x[0]*x[0]+x[1]*x[1];}require(tail>1e-8,"Reverb has no tail");
        values["reverbEnabled"]=0;fx.configure(read,120);for(int n=0;n<2000;++n)fx.tick({0,0});for(int n=0;n<1000;++n)require(fx.tick({0,0})==std::array<double,2>{},"Power bypass did not silence FX tail");
        values["reverbSend"]=0;values["eqEnabled"]=1;values["lowCut"]=400;fx.prepare(48000);fx.configure(read,120,true);double lowEnergy=0;
        for(int n=0;n<48000;++n){double input=.1*std::sin(juce::MathConstants<double>::twoPi*50*n/48000);auto x=fx.tick({input,input});if(n>24000)lowEnergy+=x[0]*x[0];}require(lowEnergy<1,"EQ low cut ineffective");
        values["eqEnabled"]=0;values["gainEqEnabled"]=1;values["gainMid"]=6;fx.prepare(48000);fx.configure(read,120,true);double gainEnergy=0;
        for(int n=0;n<24000;++n){double input=.1*std::sin(juce::MathConstants<double>::twoPi*1000*n/48000);auto x=fx.tick({input,input});if(n>=12000)gainEnergy+=x[0]*x[0];}require(gainEnergy>210&&gainEnergy<270,"Gain EQ mid-band gain incorrect");
        values["gainEqEnabled"]=0;values["grainEnabled"]=1;values["grainMix"]=100;values["grainPitch"]=12;values["grainDensity"]=40;values["grainSpray"]=0;fx.prepare(48000);fx.configure(read,120,true);std::array<double,2> real{},imag{};double grainEnergy=0;
        for(int n=0;n<48000;++n){double input=.1*std::sin(juce::MathConstants<double>::twoPi*200*n/48000);auto x=fx.tick({input,input});require(std::isfinite(x[0])&&std::abs(x[0])<2,"Granular output unbounded");if(n>=24000){grainEnergy+=x[0]*x[0];for(int b=0;b<2;++b){double phase=juce::MathConstants<double>::twoPi*(b==0?200:400)*n/48000;real[size_t(b)]+=x[0]*std::cos(phase);imag[size_t(b)]+=x[0]*std::sin(phase);}}}
        require(grainEnergy>1,"Granular resynthesis silent");require(real[1]*real[1]+imag[1]*imag[1]>100*(real[0]*real[0]+imag[0]*imag[0]),"Grain pitch did not transpose an octave");
        values["grainFreeze"]=1;values["grainReverse"]=1;fx.configure(read,120);double frozen=0;for(int n=0;n<48000;++n){auto x=fx.tick({0,0});if(n>24000)frozen+=x[0]*x[0];}require(frozen>1,"Frozen grain recording did not sustain after input stopped");
        fx.reset();for(int n=0;n<24000;++n)require(fx.tick({0,0})==std::array<double,2>{},"Granular reset retained recorded sound");values["grainEnabled"]=0;fx.configure(read,120);for(int n=0;n<2000;++n)fx.tick({0,0});require(fx.tick({.123456789,-.23456789})==std::array<double,2>{.123456789,-.23456789},"Grain bypass changed dry audio");
        values["compressEnabled"]=1;values["compressThreshold"]=-12;values["compressRatio"]=4;values["compressAttack"]=1;values["compressRelease"]=100;values["compressMix"]=100;fx.prepare(48000);fx.configure(read,120,true);std::array<double,2> compressed{};
        for(int n=0;n<24000;++n)compressed=fx.tick({.5,.25});double expected=.5*std::pow(.5/std::pow(10.,-12./20),1./4-1);require(std::abs(compressed[0]-expected)<1e-6&&std::abs(compressed[0]-2*compressed[1])<1e-9,"Compressor ratio or stereo link is incorrect");fx.configure(read,120);require(fx.gainReduction()>4&&fx.gainReduction()<5,"Compressor gain-reduction meter incorrect");
        values["compressMix"]=0;fx.configure(read,120,true);require(fx.tick({.5,.25})==std::array<double,2>{.5,.25},"Compressor zero mix not dry");values["compressMix"]=100;values["compressMakeup"]=6;fx.configure(read,120,true);auto madeUp=fx.tick({.5,.25});require(std::abs(madeUp[0]/compressed[0]-std::pow(10.,6./20))<1e-5,"Compressor makeup incorrect");values["compressEnabled"]=0;fx.configure(read,120);for(int n=0;n<2000;++n)fx.tick({.5,.25});require(fx.tick({.5,.25})==std::array<double,2>{.5,.25},"Compressor bypass not dry");
    }
    {
        handpan::ModalBankEngine dry,shaped;const std::string csv="family,group,root,velocity,mode,frequency,loss,left,right\nprobe,0,60,127,0,440,2,0.1,0.1\n";
        std::istringstream a(csv),b(csv);dry.load(a);shaped.load(b);dry.prepare(48000);shaped.prepare(48000);shaped.envelope(.1,.25,.8);dry.strike("probe",0,60,1);shaped.strike("probe",0,60,1);double onsetDry=0,onsetShaped=0,tailDry=0,tailShaped=0;
        for(int n=0;n<24000;++n){auto x=dry.tick(),y=shaped.tick();if(n<960){onsetDry+=x[0]*x[0];onsetShaped+=y[0]*y[0];}if(n>12000){tailDry+=x[0]*x[0];tailShaped+=y[0]*y[0];}}
        require(onsetShaped<onsetDry*.03,"Attack did not soften modal onset");require(tailShaped<tailDry*.01,"Decay / damping did not shorten modal vibration");
    }
    {
        auto defaults=[&]{for(int i=3;i<params.size();++i)params[i]->setValueNotifyingHost(params[i]->getDefaultValue());};
        auto render=[&]{
            set("model",6);set("articulation",0);set("gain",.6f);p->setPlayConfigDetails(0,2,48000,256);p->prepareToPlay(48000,256);std::vector<float> result;
            for(int block=0;block<96;++block){juce::AudioBuffer<float> audio(2,256);juce::MidiBuffer midi;if(block==0)midi.addEvent(juce::MidiMessage::noteOn(1,62,juce::uint8(100)),0);if(block==2)midi.addEvent(juce::MidiMessage::noteOff(1,62),0);p->processBlock(audio,midi);for(int n=0;n<256;++n){float s=audio.getSample(0,n);require(std::isfinite(s)&&std::abs(s)<=1,"New controls produced invalid plugin output");result.push_back(s);}}
            return result;
        };
        defaults();auto dry=render();set("delayEnabled",1);set("delaySync",0);set("delaySend",70);set("delayFeedback",0);set("delayTime",50);auto wet=render();double difference=0;
        for(size_t n=0;n<wet.size();++n){double expected=.3*std::atanh(double(dry[n]));if(n>=2400)expected+=.7*std::atanh(double(dry[n-2400]));require(std::abs(wet[n]-std::tanh(expected))<1e-5,"Plugin dry/wet delay mix is incorrect");if(n>=2400)difference+=std::abs(wet[n]-float(std::tanh(.3*std::atanh(double(dry[n])))));}require(difference>1,"Plugin delay send not connected to audio");
        defaults();set("attack",60);set("decay",.25f);set("damping",70);auto shaped=render();double dryOnset=0,shapedOnset=0,dryTail=0,shapedTail=0;
        for(size_t n=0;n<dry.size();++n){if(n<960){dryOnset+=dry[n]*dry[n];shapedOnset+=shaped[n]*shaped[n];}if(n>12000){dryTail+=dry[n]*dry[n];shapedTail+=shaped[n]*shaped[n];}}
        require(shapedOnset<dryOnset*.15,"Warm attack not connected to physical excitation");require(shapedTail<dryTail*.25,"Warm decay / damping ineffective");set("envEnabled",0);auto bypassed=render();require(bypassed==dry,"Envelope power bypass did not restore natural response");
        set("delaySend",37);set("eqEnabled",1);set("delayDivision",5);juce::MemoryBlock saved;p->getStateInformation(saved);std::unique_ptr<juce::AudioProcessor> copy(createPluginFilter());copy->setStateInformation(saved.getData(),int(saved.getSize()));for(int i=3;i<params.size();++i)require(std::abs(params[i]->getValue()-copy->getParameters()[i]->getValue())<1e-5,"FX / envelope state not restored");defaults();
    }
    require(dynamic_cast<juce::AudioParameterChoice*>(params[0])->choices.size()==modelCount,"All registered models exposed");
    require(p->getLatencySamples()==16,"Consistent backend latency");
    {
        handpan::Engine warm;handpan::Settings settings;settings.fieldCount=23;for(int i=0;i<23;++i)settings.tuning[size_t(i)]=50+i;warm.prepare(48000,settings);
        require(warm.silent(),"Warm was not initially asleep");for(int n=0;n<1024;++n){auto s=warm.tick();require(s.left==0&&s.right==0,"Sleeping Warm produced audio");}
        warm.envelope(0,.25,1);require(warm.strike(7,.8),"Warm wake strike rejected");require(!warm.silent(),"Warm failed to wake on strike");double energy=0,lastOutput=0;
        for(int n=0;n<144000&&!warm.silent();++n){auto s=warm.tick();energy+=s.left*s.left+s.right*s.right;lastOutput=std::max(std::abs(s.left),std::abs(s.right));}
        require(energy>1e-8&&warm.silent(),"Warm tail did not enter natural sleep");require(lastOutput<1e-8,"Warm sleep cut an audible tail");require(warm.strike(7,.8),"Warm post-sleep strike rejected");energy=0;for(int n=0;n<2048;++n){auto s=warm.tick();energy+=s.left*s.left+s.right*s.right;}require(energy>1e-8,"Warm failed to sound after natural sleep");
    }
    {
        const std::array<const char*,modelCount> names{"Orvane","Gravik","Nuvora","Virel","Serevin","Tembrel","Somera"};
        std::unique_ptr<juce::AudioProcessor> copy(createPluginFilter());
        for(int i=0;i<modelCount;++i){
            params[0]->setValueNotifyingHost(float(i)/(modelCount-1));params[1]->setValueNotifyingHost(1.f/21);
            juce::MemoryBlock data;p->getStateInformation(data);copy->setStateInformation(data.getData(),int(data.getSize()));
            auto* choice=dynamic_cast<juce::AudioParameterChoice*>(copy->getParameters()[0]);
            require(choice&&choice->getIndex()==i&&choice->getCurrentChoiceName()==names[size_t(i)],"Model name or state restore incorrect");
            require(std::abs(copy->getParameters()[1]->getValue()-1.f/21)<1e-6,"Technique not restored");
        }
    }
    {
        params[0]->setValueNotifyingHost(0);params[1]->setValueNotifyingHost(0);params[2]->setValueNotifyingHost(1);
        std::array<std::array<float,512>,4> takes{};
        for(auto& take:takes){p->prepareToPlay(48000,512);juce::AudioBuffer<float> audio(2,512);juce::MidiBuffer midi;midi.addEvent(juce::MidiMessage::noteOn(1,72,juce::uint8(100)),0);p->processBlock(audio,midi);std::copy_n(audio.getReadPointer(0),512,take.begin());}
        require(takes[0]!=takes[1]&&takes[1]!=takes[2]&&takes[0]==takes[3],"Three-way strike alternation did not cycle");
    }
    {
        params[0]->setValueNotifyingHost(1);params[1]->setValueNotifyingHost(0);p->prepareToPlay(48000,256);
        juce::AudioBuffer<float> audio(2,256);juce::MidiBuffer midi;midi.addEvent(juce::MidiMessage::noteOn(1,keyswitchBase+2,juce::uint8(100)),20);p->processBlock(audio,midi);require(audio.getMagnitude(0,256)==0,"Keyswitch sounded a note");
        for(int wait=0;wait<20&&std::abs(params[1]->getValue()-2.f/21)>1e-6;++wait){juce::Thread::sleep(20);juce::Timer::callPendingTimersSynchronously();}require(std::abs(params[1]->getValue()-2.f/21)<1e-6,"Keyswitch did not synchronize host articulation");
        juce::MemoryBlock data;p->getStateInformation(data);std::unique_ptr<juce::AudioProcessor> copy(createPluginFilter());copy->setStateInformation(data.getData(),int(data.getSize()));require(std::abs(copy->getParameters()[1]->getValue()-2.f/21)<1e-6,"Keyswitch state not saved");
        for(int model=0;model<modelCount;++model)for(int art=0;art<int(modelRegistry()[size_t(model)].articulations.size());++art){
            params[0]->setValueNotifyingHost(float(model)/(modelCount-1));params[1]->setValueNotifyingHost(0);p->prepareToPlay(48000,256);midi.addEvent(juce::MidiMessage::noteOn(1,keyswitchForArticulation(model,art),juce::uint8(80)),0);p->processBlock(audio,midi);require(audio.getMagnitude(0,256)==0,"Keyswitch produced audio");for(int wait=0;wait<20&&std::abs(params[1]->getValue()-float(art)/21)>1e-6;++wait){juce::Thread::sleep(20);juce::Timer::callPendingTimersSynchronously();}require(std::abs(params[1]->getValue()-float(art)/21)<1e-6,"Keyswitch wrong model/technique");
        }
    }
    {
        std::unique_ptr<juce::AudioProcessor> switched(createPluginFilter()),reference(createPluginFilter());
        for(auto* instrument:{switched.get(),reference.get()}){instrument->getParameters()[0]->setValueNotifyingHost(1);instrument->setPlayConfigDetails(0,2,48000,256);}
        switched->getParameters()[1]->setValueNotifyingHost(0);reference->getParameters()[1]->setValueNotifyingHost(1.f/21);switched->prepareToPlay(48000,256);reference->prepareToPlay(48000,256);
        juce::AudioBuffer<float> a(2,256),b(2,256);juce::MidiBuffer ma,mb;
        ma.addEvent(juce::MidiMessage::noteOn(1,keyswitchBase+1,juce::uint8(100)),37);ma.addEvent(juce::MidiMessage::noteOn(1,62,juce::uint8(100)),37);mb.addEvent(juce::MidiMessage::noteOn(1,62,juce::uint8(100)),37);
        switched->processBlock(a,ma);reference->processBlock(b,mb);for(int c=0;c<2;++c)for(int n=0;n<256;++n)require(a.getSample(c,n)==b.getSample(c,n),"Keyswitch/note ordering was not sample accurate");
        juce::MemoryBlock state;switched->getStateInformation(state);std::unique_ptr<juce::AudioProcessor> copy(createPluginFilter());copy->setStateInformation(state.getData(),int(state.getSize()));require(std::abs(copy->getParameters()[1]->getValue()-1.f/21)<1e-6,"Immediate keyswitch state lost before UI sync");
        switched->getParameters()[1]->setValueNotifyingHost(2.f/21);reference->getParameters()[1]->setValueNotifyingHost(2.f/21);
        ma.addEvent(juce::MidiMessage::noteOn(1,65,juce::uint8(80)),0);mb.addEvent(juce::MidiMessage::noteOn(1,65,juce::uint8(80)),0);switched->processBlock(a,ma);reference->processBlock(b,mb);
        for(int c=0;c<2;++c)for(int n=0;n<256;++n)require(a.getSample(c,n)==b.getSample(c,n),"Host automation did not override MIDI articulation");
    }
    for(int i:{0,1,2,3,4,6})require(modelRegistry()[size_t(i)].family==InstrumentFamily::Handpan,"Handpan classification");
    for(int i:{5})require(modelRegistry()[size_t(i)].family==InstrumentFamily::TongueDrum,"Tongue drum classification");
    {
        std::unique_ptr<juce::AudioProcessorEditor> editor(p->createEditorIfNeeded());
        auto* chooser=dynamic_cast<juce::ComboBox*>(editor->findChildWithID("instrumentSelector"));require(chooser!=nullptr,"Instrument selector missing");
        for(int id:{5,6,1,7}){chooser->setSelectedId(id,juce::sendNotificationSync);require(std::abs(params[0]->getValue()-float(id-1)/(modelCount-1))<1e-6,"Grouped selector changed model identity");}
        chooser->setSelectedId(5,juce::sendNotificationSync);juce::Thread::sleep(100);juce::Timer::callPendingTimersSynchronously();
        auto* articulation=dynamic_cast<juce::ComboBox*>(editor->findChildWithID("articulationSelector"));require(articulation!=nullptr,"Articulation selector missing");articulation->setSelectedId(3,juce::sendNotificationSync);juce::Thread::sleep(100);juce::Timer::callPendingTimersSynchronously();
        auto* keyboard=dynamic_cast<juce::MidiKeyboardComponent*>(editor->findChildWithID("instrumentKeyboard"));require(keyboard&&keyboard->getRangeStart()<=57&&keyboard->getRangeEnd()==72,"Serevin range not fitted");
        auto* down=dynamic_cast<juce::TextButton*>(editor->findChildWithID("keyboardOctaveDown"));
        auto* up=dynamic_cast<juce::TextButton*>(editor->findChildWithID("keyboardOctaveUp"));
        auto* fit=dynamic_cast<juce::TextButton*>(editor->findChildWithID("keyboardFit"));require(down&&up&&fit,"Keyboard range controls missing");
        require(editor->findChildWithID("advancedArticulations")==nullptr,"Advanced control remained");
        for(int id:{1,2,3,4,5,6,7}){
            chooser->setSelectedId(id,juce::sendNotificationSync);juce::Thread::sleep(40);juce::Timer::callPendingTimersSynchronously();
            const bool tongue=modelRegistry()[size_t(id-1)].family==InstrumentFamily::TongueDrum;
            require(editor->getProperties()["theme"].toString()==(tongue?"calibre":"nodal"),"Instrument family theme mismatch");
            require(editor->getWidth()==1000&&editor->getHeight()==640,"Shared design dimensions changed");
            const int count=int(editor->getProperties()["stageZoneCount"]);require(count==(tongue?8:9),"Unexpected clickable surface area added");
            for(int art=1;art<=articulation->getNumItems();++art){articulation->setSelectedId(art,juce::sendNotificationSync);juce::Thread::sleep(35);juce::Timer::callPendingTimersSynchronously();require(int(editor->getProperties()["stageZoneCount"])==count,"Articulation changed physical geometry");}
            articulation->setSelectedId(1,juce::sendNotificationSync);juce::Thread::sleep(35);juce::Timer::callPendingTimersSynchronously();
            for(int wait=0;wait<20&&int(editor->getProperties()["activeArticulation"])!=0;++wait){juce::Thread::sleep(20);juce::Timer::callPendingTimersSynchronously();}
            require(int(editor->getProperties()["activeArticulation"])==0&&int(editor->getProperties()["activeKeyswitch"])==keyswitchBase,"Stopped playback retained stale MIDI articulation");
            const int low=keyboard->getRangeStart(),high=keyboard->getRangeEnd();
            int playableLow=id==7?50:127,playableHigh=id==7?72:0;
            if(id!=7)for(auto z:panKeyMap().groups[size_t(modelRegistry()[size_t(id-1)].articulations[0].group)]){playableLow=std::min(playableLow,z.low);playableHigh=std::max(playableHigh,z.high);}
            require(high==playableHigh&&low<=std::max(0,playableLow-24),"Keyboard fit lost high cutoff or low keyswitch reserve");
            float right=0;for(int key=low;key<=high;++key)right=std::max(right,keyboard->getRectangleForKey(key).getRight());
            require(std::abs(right-keyboard->getWidth())<1,"Keyboard failed to fill available width");
            auto modelValue=params[0]->getValue();down->onClick();require(keyboard->getRangeStart()==std::max(0,low-12),"Keyboard octave down failed");
            fit->onClick();require(keyboard->getRangeStart()==low&&keyboard->getRangeEnd()==high,"Keyboard fit reset failed");
            up->onClick();require(keyboard->getRangeEnd()==std::min(127,high+12),"Keyboard octave up failed");fit->onClick();require(params[0]->getValue()==modelValue,"Keyboard navigation changed sound model");
            require(int(editor->getProperties()["visibleArticulationCount"])==int(modelRegistry()[size_t(id-1)].articulations.size()),"Retained techniques hidden");
            if(id==1||id==6||id==7){
                auto image=editor->createComponentSnapshot(editor->getLocalBounds());
                auto file=juce::File::getCurrentWorkingDirectory().getChildFile("ui-preview-"+juce::String(id)+".png");
                auto stream=file.createOutputStream();require(stream!=nullptr,"UI preview output unavailable");stream->setPosition(0);stream->truncate();juce::PNGImageFormat format;require(format.writeImageToStream(image,*stream),"UI preview failed");
            }
        }
        require(editor->findChildWithID("stopSound")==nullptr&&editor->findChildWithID("position")==nullptr&&editor->findChildWithID("hardness")==nullptr,"Removed controls remained");
        require(editor->isResizable(),"Editor is not resizable");editor->setSize(1400,896);require(editor->getWidth()==1400&&editor->getHeight()==896,"Large editor size rejected");
        {auto img=editor->createComponentSnapshot(editor->getLocalBounds());auto f=juce::File::getCurrentWorkingDirectory().getChildFile("ui-resized.png");auto stream=f.createOutputStream();stream->setPosition(0);stream->truncate();juce::PNGImageFormat format;format.writeImageToStream(img,*stream);}
        editor->setSize(800,512);require(editor->getWidth()==800,"Small editor size rejected");editor->setSize(1000,640);
        auto* fx=dynamic_cast<juce::TextButton*>(editor->findChildWithID("effectsToggle"));auto* panel=editor->findChildWithID("effectsPanel");require(fx&&fx->getButtonText()=="FX"&&panel&&!panel->isVisible(),"FX overlay default visibility");
        for(int id:{1,6}){
            chooser->setSelectedId(id,juce::sendNotificationSync);juce::Timer::callPendingTimersSynchronously();fx->setToggleState(true,juce::dontSendNotification);fx->onClick();require(panel->isVisible()&&keyboard->isVisible(),"FX overlay hid keyboard");
            set("delayEnabled",0);auto* power=dynamic_cast<juce::Button*>(findComponent(*panel,"delayEnabled"));require(power&&!power->getToggleState(),"FX power did not follow host parameter");
            set("delayEnabled",1);auto* mainSend=editor->findChildWithID("delaySend");auto* fullSend=findComponent(*panel,"delaySend");require(mainSend&&fullSend,"Shared delay mix missing");
            auto* mainSlider=dynamic_cast<juce::Slider*>(mainSend->findChildWithID("delaySendSlider"));auto* fullSlider=dynamic_cast<juce::Slider*>(fullSend->findChildWithID("delaySendSlider"));require(mainSlider&&fullSlider,"Mix knob missing");mainSlider->setValue(37,juce::sendNotificationSync);require(std::abs(fullSlider->getValue()-37)<1e-4,"Main/FX mix controls diverged");set("delaySend",0);
            require(mainSlider->getSliderStyle()==juce::Slider::RotaryHorizontalVerticalDrag&&fullSlider->getSliderStyle()==juce::Slider::RotaryHorizontalVerticalDrag,"FX control is not rotary");
            require(panel->findChildWithID("fxTab0")==nullptr,"Paged FX controls remained");require(panel->findChildWithID("effectsScrollLeft")==nullptr&&panel->findChildWithID("effectsScrollRight")==nullptr,"FX navigation buttons remained");
            auto* viewport=dynamic_cast<juce::Viewport*>(panel->findChildWithID("effectsViewport"));require(viewport&&int(panel->getProperties()["effectsModuleCount"])==6,"Six-module rack missing");auto* rack=viewport->getViewedComponent();require(rack&&rack->getNumChildComponents()==6,"FX modules not simultaneously present");
            for(int i=0;i<6;++i){auto* module=rack->findChildWithID("fxModule"+juce::String(i));require(module&&module->isVisible(),"FX module hidden");}
            require(viewport->getViewWidth()>=3*210,"Rack does not show multiple modules");
            for(int view=0;view<2;++view){viewport->setViewPosition(view==0?0:rack->getWidth()-viewport->getViewWidth(),0);auto position=viewport->getViewPositionX();require(view==0?position==0:position>0,"FX horizontal scroll failed");
                auto img=editor->createComponentSnapshot(editor->getLocalBounds());auto f=juce::File::getCurrentWorkingDirectory().getChildFile("ui-rack-"+juce::String(id)+"-"+juce::String(view)+".png");auto stream=f.createOutputStream();stream->setPosition(0);stream->truncate();juce::PNGImageFormat format;format.writeImageToStream(img,*stream);
            }
            viewport->setViewPosition(0,0);
            set("attack",123);set("grainEnabled",1);set("grainMix",84);set("gainLow",6);auto* reset=dynamic_cast<juce::TextButton*>(panel->findChildWithID("initialiseEffects"));require(reset!=nullptr,"Initialise FX missing");auto oldModel=params[0]->getValue();reset->onClick();require(params[0]->getValue()==oldModel&&std::abs(parameter("attack")->convertFrom0to1(parameter("attack")->getValue())-123)<.001,"Initialise FX changed the instrument / envelope");for(const char* id:{"eqEnabled","gainEqEnabled","delayEnabled","reverbEnabled","grainEnabled","compressEnabled"})require(parameter(id)->getValue()==0,"Initialise FX left a module enabled");require(parameter("grainMix")->getValue()==parameter("grainMix")->getDefaultValue()&&parameter("gainLow")->convertFrom0to1(parameter("gainLow")->getValue())==0,"Initialise FX did not reset knobs");set("attack",0);
            auto* close=dynamic_cast<juce::TextButton*>(panel->findChildWithID("closeEffects"));require(close!=nullptr,"FX close missing");close->onClick();require(!panel->isVisible()&&!fx->getToggleState(),"FX close did not restore surface");
        }
        chooser->setSelectedId(2,juce::sendNotification);
        juce::Thread::sleep(40);juce::Timer::callPendingTimersSynchronously();
        require(chooser->getSelectedId()==2,"UI refresh cancelled pending model selection");
        chooser->onChange();
        articulation->setSelectedId(2,juce::sendNotification);
        juce::Thread::sleep(40);juce::Timer::callPendingTimersSynchronously();
        require(articulation->getSelectedId()==2,"UI refresh cancelled pending articulation selection");
        articulation->onChange();
    }
    // Preserve transposed zone edges and reject keys beyond them.
    for(int group=0;group<=10;++group)for(auto z:panKeyMap().groups[size_t(group)])require(z.root!=53,"Removed Gu source resurrected");
    for(int group=12;group<=22;++group)for(auto z:panKeyMap().groups[size_t(group)])require(z.root!=50,"Removed Gu source resurrected");
    for(int group:{11,83,88})require(panKeyMap().root(group,127)<0,"Removed noise key remained");
    require(panKeyMap().root(0,53)==65&&panKeyMap().root(12,50)==62,"Orvane / Gravik pitch routing");
    require(panKeyMap().root(24,47)==59&&panKeyMap().root(46,48)==60,"Nuvora / Virel pitch routing");
    require(panKeyMap().root(84,45)==69&&panKeyMap().root(84,64)==88&&panKeyMap().root(84,44)<0,"Tembrel pitch routing");
    require(panKeyMap().root(80,50)==62&&panKeyMap().root(81,50)==62&&panKeyMap().root(82,57)==21,"Serevin native pitch routing");
    require(panKeyMap().root(82,50)<0&&panKeyMap().root(80,73)<0,"Serevin missing Ding fabricated");
    for(int model=0;model<6;++model){
        params[0]->setValueNotifyingHost(float(model)/(modelCount-1));
        for(size_t art=0;art<modelRegistry()[size_t(model)].articulations.size();++art){
            params[1]->setValueNotifyingHost(float(art)/21);
            int group=modelRegistry()[size_t(model)].articulations[art].group;
            for(int key=0;key<128;++key)if(panKeyMap().root(group,key)<0){
                p->prepareToPlay(48000,256);juce::AudioBuffer<float> audio(2,256);juce::MidiBuffer midi;
                midi.addEvent(juce::MidiMessage::noteOn(1,key,juce::uint8(100)),0);p->processBlock(audio,midi);
                require(audio.getMagnitude(0,256)==0,"Pan model played outside original key zones");
            }
        }
    }
    for(double rate:{32000.,48000.,96000.}){
        p->setPlayConfigDetails(0,2,rate,256);p->prepareToPlay(rate,256);
        for(int model=0;model<modelCount;++model){
            params[0]->setValueNotifyingHost(float(model)/(modelCount-1));params[1]->setValueNotifyingHost(0);
            p->releaseResources();p->prepareToPlay(rate,256);
            juce::AudioBuffer<float> audio(2,256);juce::MidiBuffer midi;
            int testKey=model==6?62:72;
            if(model<6){int group=modelRegistry()[size_t(model)].articulations.front().group;for(int key=0;key<128;++key)if(panKeyMap().root(group,key)>=0){testKey=key;break;}}
            midi.addEvent(juce::MidiMessage::noteOn(1,testKey,juce::uint8(100)),37);
            p->processBlock(audio,midi);double energy=0;
            for(int c=0;c<2;++c)for(int n=0;n<256;++n){auto x=audio.getSample(c,n);require(std::isfinite(x)&&std::abs(x)<=1,"Audio finite/headroom");if(n<37)require(x==0,"MIDI sample offset");energy+=x*x;}
            require(energy>1e-15,"Model silent");
        }
    }
    for(int model=0;model<6;++model)for(size_t art=0;art<modelRegistry()[size_t(model)].articulations.size();++art){
        params[0]->setValueNotifyingHost(float(model)/(modelCount-1));params[1]->setValueNotifyingHost(float(art)/21);const int group=modelRegistry()[size_t(model)].articulations[art].group;
        for(int key=0;key<128;++key)if(panKeyMap().root(group,key)>=0){
            p->setPlayConfigDetails(0,2,48000,512);p->prepareToPlay(48000,512);juce::AudioBuffer<float> audio(2,512);juce::MidiBuffer midi;midi.addEvent(juce::MidiMessage::noteOn(1,key,juce::uint8(100)),0);p->processBlock(audio,midi);double energy=0;
            for(int c=0;c<2;++c)for(int n=0;n<512;++n){auto x=audio.getSample(c,n);require(std::isfinite(x)&&std::abs(x)<=1,"Mapped note invalid");energy+=x*x;}require(energy>1e-15,"Mapped articulation/key silent");
        }
    }
    {
        auto late=std::make_unique<handpan::StrikeTail>();std::istringstream data(std::string(BinaryData::strike_tail_csv,size_t(BinaryData::strike_tail_csvSize)));late->load(data);
        for(double rate:{32000.,48000.,96000.})for(int key:{25,26}){
            late->prepare(rate);late->strike(key,.7);require(late->tick()==std::array<double,2>{},"Late onset discontinuity");
            double error=0,energy=0;const double velocity=.7*127,mix=(velocity-63)/28;
            struct Mode{double f,slow,fast,amp;};std::array<Mode,16> mode{};
            std::istringstream rows(std::string(BinaryData::strike_tail_csv,size_t(BinaryData::strike_tail_csvSize)));std::string line;std::getline(rows,line);
            while(std::getline(rows,line)){std::istringstream row(line);std::string token;std::array<double,8> v{};for(auto& x:v){std::getline(row,token,',');x=std::stod(token);}if(v[0]==26&&(v[1]==63||v[1]==91)){auto& m=mode[size_t(v[2])];m.f=v[3];m.slow=v[4];m.fast=v[5];m.amp+=v[6]*(v[1]==63?1-mix:mix);}}
            for(int i=1;i<int(rate*2);++i){auto actual=late->tick();double expected=0,t=i/rate;for(auto m:mode)expected-=std::sin(6.283185307179586*m.f*std::pow(2.,(key-26)/12.)*t)*std::pow(std::exp(-m.slow*t)-std::exp(-m.fast*t),3)*m.amp;error=std::max(error,std::abs(actual[0]-expected));for(double x:actual){require(std::isfinite(x),"Late nonfinite");energy+=x*x;}}
            require(error<1e-9&&energy>1e-10,"Late analytic envelope parity");late->reset();for(int i=0;i<64;++i)require(late->tick()==std::array<double,2>{},"Late reset tail");
        }
        late->prepare(48000);late->strike(26,.7);std::array<std::array<double,2>,4096> single{};for(auto& s:single)s=late->tick();
        late->reset();late->strike(26,.7);late->strike(26,.7);for(auto s:single){auto actual=late->tick();for(int c=0;c<2;++c)require(std::abs(actual[c]-2*s[c])<1e-11,"Late coherent contacts");}
        late->reset();late->strike(26,.7);late->damp();for(auto s:single){auto actual=late->tick();for(int c=0;c<2;++c)require(std::abs(actual[c]-.05*s[c])<1e-11,"Late damp ratio");}
    }
    params[0]->setValueNotifyingHost(6.f/(modelCount-1));params[1]->setValueNotifyingHost(2.f/21);params[2]->setValueNotifyingHost(.37f);set("compressThreshold",-24);set("compressRelease",180);
    juce::MemoryBlock saved;p->getStateInformation(saved);
    std::unique_ptr<juce::AudioProcessor> restored(createPluginFilter());restored->setStateInformation(saved.getData(),int(saved.getSize()));
    for(int i=0;i<params.size();++i)require(std::abs(params[i]->getValue()-restored->getParameters()[i]->getValue())<1e-5,"State restore");
    p->setPlayConfigDetails(0,2,48000,256);p->prepareToPlay(48000,256);
    for(int k=0;k<100;++k){params[0]->setValueNotifyingHost(float(k%modelCount)/(modelCount-1));juce::AudioBuffer<float> audio(2,256);juce::MidiBuffer midi;midi.addEvent(juce::MidiMessage::noteOn(1,60+k%24,juce::uint8(127)),0);p->processBlock(audio,midi);for(int c=0;c<2;++c)for(int n=0;n<256;++n)require(std::isfinite(audio.getSample(c,n)),"Switch stress");}
    for(int model:{6}){
        params[0]->setValueNotifyingHost(float(model)/(modelCount-1));params[2]->setValueNotifyingHost(1);
        for(int technique=0;technique<3;++technique){
            params[1]->setValueNotifyingHost(float(technique)/21);
            for(int key=0;key<128;++key){
                p->setPlayConfigDetails(0,2,48000,256);p->prepareToPlay(48000,256);
                const bool supported=key>=50&&key<=72;
                double energy=0;
                for(int block=0;block<2;++block){juce::AudioBuffer<float> audio(2,256);juce::MidiBuffer midi;
                    if(block==0)midi.addEvent(juce::MidiMessage::noteOn(1,key,juce::uint8(100)),0);p->processBlock(audio,midi);
                    for(int c=0;c<2;++c)for(int n=0;n<256;++n){float x=audio.getSample(c,n);require(std::isfinite(x)&&std::abs(x)<=1,"Expanded field output invalid");energy+=x*x;}
                }
                require(supported?energy>1e-15:energy==0,"Somera D3-C5 continuous chromatic contract");
            }
        }
    }
    {
        handpan::Engine warm;handpan::Settings settings;settings.fieldCount=23;for(int i=0;i<23;++i)settings.tuning[size_t(i)]=50+i;warm.prepare(48000,settings);
        for(int i=0;i<23;++i)require(std::abs(warm.frequency(i)-440*std::pow(2.,(50+i-69)/12.))<1e-8,"Warm chromatic field detuned");
        require(warm.strike(22,.8),"Warm C5 field missing");
    }
    for(int model:{0,1,2,3,4}){
        params[0]->setValueNotifyingHost(float(model)/(modelCount-1));params[1]->setValueNotifyingHost(0);p->releaseResources();p->prepareToPlay(48000,256);
        for(int block=0;block<12;++block){juce::AudioBuffer<float> audio(2,256);juce::MidiBuffer midi;for(int key=57;key<73;++key)midi.addEvent(juce::MidiMessage::noteOn(1,key,juce::uint8(127)),0);p->processBlock(audio,midi);for(int c=0;c<2;++c)for(int n=0;n<256;++n)require(std::isfinite(audio.getSample(c,n))&&std::abs(audio.getSample(c,n))<=1,"Handpan dynamics dense MIDI invalid");}
        juce::AudioBuffer<float> audio(2,256);juce::MidiBuffer midi;midi.addEvent(juce::MidiMessage::allSoundOff(1),17);p->processBlock(audio,midi);for(int c=0;c<2;++c)for(int n=17;n<256;++n)require(audio.getSample(c,n)==0,"Handpan panic did not reset output delay");
    }
    params[0]->setValueNotifyingHost(4.f/(modelCount-1));params[1]->setValueNotifyingHost(2.f/21);params[2]->setValueNotifyingHost(.6f);
    for(int key:{57,60,62,64,65,67,69,72}){
        std::array<double,3> energies{};int index=0;
        for(int velocity:{24,64,110}){
            p->releaseResources();p->prepareToPlay(48000,256);
            for(int block=0;block<16;++block){juce::AudioBuffer<float> audio(2,256);juce::MidiBuffer midi;if(block==0)midi.addEvent(juce::MidiMessage::noteOn(1,key,juce::uint8(velocity)),37);p->processBlock(audio,midi);
                for(int c=0;c<2;++c)for(int n=0;n<256;++n){float x=audio.getSample(c,n);require(std::isfinite(x)&&std::abs(x)<=1,"Serevin Corner invalid output");energies[size_t(index)]+=x*x;}
            }++index;
        }
        require(*std::min_element(energies.begin(),energies.end())>1e-12,"Serevin Corner silent");
        require(std::abs(energies[1]-energies[0])>1e-5*std::max(energies[1],energies[0])&&std::abs(energies[2]-energies[1])>1e-5*std::max(energies[2],energies[1]),"Serevin Corner velocity plateau");
    }
    {
        auto texture=std::make_unique<handpan::StrikeTexture>();std::istringstream data(std::string(BinaryData::strike_texture_csv,size_t(BinaryData::strike_texture_csvSize)));texture->load(data);
        for(double rate:{32000.,48000.,96000.}){
            texture->prepare(rate);texture->strike(24,.7);double energy=0;
            for(int i=0;i<4096;++i)for(double x:texture->tick()){require(std::isfinite(x),"Texture nonfinite");energy+=x*x;}
            require(energy>1e-10,"Texture silent");texture->reset();for(int i=0;i<64;++i)require(texture->tick()==std::array<double,2>{},"Texture reset tail");
        }
        texture->prepare(48000);texture->strike(24,.7);std::array<std::array<double,2>,256> single{};for(auto& s:single)s=texture->tick();
        texture->reset();texture->strike(24,.7);texture->strike(24,.7);
        for(auto s:single){auto actual=texture->tick();for(int c=0;c<2;++c)require(std::abs(actual[size_t(c)]-std::sqrt(2.)*s[size_t(c)])<1e-12,"Texture contacts did not add power");}
        texture->reset();texture->strike(24,.7);texture->damp();for(auto s:single){auto actual=texture->tick();for(int c=0;c<2;++c)require(std::abs(actual[size_t(c)]-.05*s[size_t(c)])<1e-12,"Texture damping inconsistent");}
        params[0]->setValueNotifyingHost(4.f/(modelCount-1));params[1]->setValueNotifyingHost(2.f/21);p->releaseResources();p->prepareToPlay(48000,256);
        double first=0,last=0;
        for(int block=0;block<1200;++block){juce::AudioBuffer<float> audio(2,256);juce::MidiBuffer midi;if(block<12)for(int key:{57,60,62,64,65,67,69,72})midi.addEvent(juce::MidiMessage::noteOn(1,key,juce::uint8(127)),0);p->processBlock(audio,midi);
            for(int c=0;c<2;++c)for(int n=0;n<256;++n){double x=audio.getSample(c,n);require(std::isfinite(x)&&std::abs(x)<=1,"Dense C texture invalid");if(block<24)first+=x*x;if(block>1175)last+=x*x;}}
        require(last<first*.02,"Dense C texture did not decay");
        juce::AudioBuffer<float> audio(2,256);juce::MidiBuffer midi;midi.addEvent(juce::MidiMessage::allSoundOff(1),17);p->processBlock(audio,midi);for(int c=0;c<2;++c)for(int n=17;n<256;++n)require(audio.getSample(c,n)==0,"Texture panic tail");
    }
    std::cout<<"PASS "<<modelCount<<" models x 3 rates; MIDI sample offsets; state restore; 100 switches; Somera D3-C5 chromatic range x six techniques; Serevin Corner eight-key dynamics; texture rates, power accumulation, damp, dense decay and reset\n";return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}

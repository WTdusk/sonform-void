#include <juce_audio_utils/juce_audio_utils.h>
#include "BinaryData.h"
#include "../src/ModalBankEngine.h"
#include "../src/PanDrumProfiles.h"
#include "../src/StrikeTexture.h"
#include "../src/StrikeTail.h"
#include "ModelRegistry.h"
#include "PanKeyMap.h"
#include "../src/Performance.h"
#include "SoundControls.h"

#include <sstream>

// The frontend depends on this contract, not on modal parameters or model topology.
struct InstrumentBackend {
    virtual ~InstrumentBackend()=default;
    virtual void prepare(double rate)=0;
    virtual void reset()=0;
    virtual void damp()=0;
    virtual void strike(int group,int key,double velocity)=0;
    virtual void envelope(double,double,double)=0;
    virtual std::array<double,2> tick()=0;
    virtual bool silent()const=0;
};
struct ModalBackend final:InstrumentBackend {
    handpan::ModalBankEngine engine;
    std::string family;
    std::unique_ptr<handpan::StrikeTexture> texture;
    std::unique_ptr<handpan::StrikeTail> late;
    std::array<std::array<double,2>,16> delay{};
    size_t delayIndex=0;bool sourceIdle=true;double responseGain=1;
    ModalBackend(const handpan::ModalBankEngine& prototype,int index):engine(prototype),family(modelRegistry()[size_t(index)].id){
        (void)panKeyMap();
        responseGain=engine.familyGain(family);
        if(index==4){
            texture=std::make_unique<handpan::StrikeTexture>();std::istringstream bands(std::string(BinaryData::strike_texture_csv,size_t(BinaryData::strike_texture_csvSize)));texture->load(bands);
            late=std::make_unique<handpan::StrikeTail>();std::istringstream tail(std::string(BinaryData::strike_tail_csv,size_t(BinaryData::strike_tail_csvSize)));late->load(tail);
        }
    }
    void prepare(double r) override{engine.prepare(r);if(texture)texture->prepare(r);if(late)late->prepare(r);delay={};delayIndex=0;sourceIdle=true;}
    void reset() override{engine.reset();if(texture)texture->reset();if(late)late->reset();delay={};delayIndex=0;sourceIdle=true;}
    void damp() override{engine.release();if(texture)texture->damp();if(late)late->damp();}
    void envelope(double a,double d,double damp)override{engine.envelope(a,d,damp);if(texture)texture->envelope(a,d,damp);if(late)late->envelope(a,d,damp);sourceIdle=engine.silent()&&(!texture||texture->silent())&&(!late||late->silent());}
    void strike(int g,int k,double v) override{const int root=panKeyMap().root(g,k);if(root<0)return;const int source=PanKeyMap::sourceKey(g,k);engine.strike(family,g,source,v,root);sourceIdle=false;if(texture&&g==82)texture->strike(source,v);if(late&&g==82)late->strike(source,v);}
    bool silent()const override{if(!sourceIdle)return false;for(auto& sample:delay)if(sample[0]!=0||sample[1]!=0)return false;return true;}
    std::array<double,2> tick() override{auto out=delay[delayIndex];std::array<double,2> current{};if(!sourceIdle){current=engine.tick();if(texture){auto noise=texture->tick();for(int c=0;c<2;++c)current[size_t(c)]+=noise[size_t(c)]*responseGain;}if(late){auto resonance=late->tick();for(int c=0;c<2;++c)current[size_t(c)]+=resonance[size_t(c)]*responseGain;}}delay[delayIndex]=current;delayIndex=(delayIndex+1)%delay.size();return out;}
};
struct WarmBackend final:InstrumentBackend {
    handpan::Engine engine;
    void prepare(double rate)override{handpan::Settings settings;settings.fieldCount=23;for(int i=0;i<23;++i)settings.tuning[size_t(i)]=50+i;engine.prepare(rate,settings);}
    void reset()override{engine.reset();}
    void damp()override{for(int f=0;f<handpan::Engine::fields;++f)engine.touch(f,1);}
    void envelope(double a,double d,double damp)override{engine.envelope(a,d,damp);}
    void strike(int group,int key,double velocity)override{if(key<50||key>72)return;int field=key-50;engine.touch(field,0);engine.strike(field,velocity,.25,.4,handpan::Technique(std::clamp(group,0,5)));}
    bool silent()const override{return engine.silent();}
    std::array<double,2> tick()override{auto s=engine.tick();return {s.left,s.right};}
};
class Processor final:public juce::AudioProcessor,private juce::Timer,private juce::MidiKeyboardState::Listener {
public:
    juce::AudioProcessorValueTreeState state;
    juce::MidiKeyboardState keyboard;
    std::atomic<bool> resetFx{false}, restoreFx{false};
    // The UI owns MidiKeyboardState. Only this bounded note queue crosses to audio.
    struct UiStrike { int key; float velocity; };
    juce::AbstractFifo uiQueue{257};
    std::array<UiStrike,257> uiStrikes{};
    juce::MidiBuffer keyboardDrain;
    void handleNoteOn(juce::MidiKeyboardState*,int,int key,float velocity) override {
        auto slot=uiQueue.write(1);
        if(slot.blockSize1)uiStrikes[size_t(slot.startIndex1)]={key,velocity};
    }
    void handleNoteOff(juce::MidiKeyboardState*,int,int,float) override {}
    std::atomic<int> midiArt{-1},midiArtModel{-1},pendingArt{-1},pendingModel{-1},pendingExpected{-1},observedHostArt{0},midiRevision{0},pendingRevision{0},syncedRevision{-1};
    int displayedArticulation(int model)const{int a=midiArt.load();return a>=0&&syncedRevision.load()!=midiRevision.load()&&midiArtModel.load()==model&&int(state.getRawParameterValue("articulation")->load())==observedHostArt.load()?a:int(state.getRawParameterValue("articulation")->load());}
    void timerCallback()override{
        keyboard.processNextMidiBuffer(keyboardDrain,0,1,false);
        int a=pendingArt.exchange(-1);const int revision=pendingRevision.load();
        if(a>=0&&pendingModel.load()==int(state.getRawParameterValue("model")->load())&&midiArt.load()==a&&midiRevision.load()==revision&&pendingExpected.load()==int(state.getRawParameterValue("articulation")->load())){
            auto* parameter=state.getParameter("articulation");parameter->beginChangeGesture();parameter->setValueNotifyingHost(parameter->convertTo0to1(float(a)));parameter->endChangeGesture();syncedRevision=revision;
        }
    }
    std::array<std::atomic<float>,128> visualHits{};
    std::array<std::atomic<unsigned>,128> hostKeyChannels{};
    std::atomic<float> outputPeak{0};
    std::array<std::unique_ptr<InstrumentBackend>,modelCount> backends;
    std::array<std::array<unsigned,128>,89> rotations{};
    juce::SmoothedValue<float> gain;
    StudioEffects effects;
    int active=0,fade=0,fadeLength=480;
    std::array<double,modelCount> weights{},fadeStart{};
    Processor():AudioProcessor(BusesProperties().withOutput("Output",juce::AudioChannelSet::stereo(),true)),
        state(*this,nullptr,"SonformVoidState",layout()){
        for(auto& hit:visualHits)hit.store(0,std::memory_order_relaxed);
        std::istringstream data(std::string(BinaryData::bank_csv, size_t(BinaryData::bank_csvSize)));
        handpan::ModalBankEngine prototype;prototype.load(data);
        std::array<std::string_view,5> dynamicsIDs{};
        for(size_t i=0;i<dynamicsIDs.size();++i)dynamicsIDs[i]=modelRegistry()[i].id;
        handpan::configureHandpanDynamics(prototype,dynamicsIDs);
        for(size_t i=0;i<backends.size();++i){
            if(i==6)backends[i]=std::make_unique<WarmBackend>();
            else backends[i]=std::make_unique<ModalBackend>(prototype,int(i));
        }
        effects.bind(state);keyboard.addListener(this);setLatencySamples(16);startTimerHz(30);
    }
    ~Processor()override{stopTimer();keyboard.removeListener(this);}
    static juce::AudioProcessorValueTreeState::ParameterLayout layout(){
        juce::StringArray models;for(auto&m:modelRegistry())models.add(m.name);
        juce::AudioProcessorValueTreeState::ParameterLayout l;
        l.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{"model",1},"Model",models,0));
        l.add(std::make_unique<juce::AudioParameterInt>(juce::ParameterID{"articulation",1},"Articulation",0,21,0));
        l.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"gain",1},"Output",juce::NormalisableRange<float>(0,1),.85f));
        addSoundParameters(l);return l;
    }
    const juce::String getName()const override{return "SONFORM: Void";}
    bool acceptsMidi()const override{return true;}bool producesMidi()const override{return false;}bool isMidiEffect()const override{return false;}
    double getTailLengthSeconds()const override{return state.getRawParameterValue("grainEnabled")->load()>.5f&&state.getRawParameterValue("grainFreeze")->load()>.5f?std::numeric_limits<double>::infinity():60;}
    int getNumPrograms()override{return 1;}int getCurrentProgram()override{return 0;}
    void setCurrentProgram(int)override{}const juce::String getProgramName(int)override{return {};}
    void changeProgramName(int,const juce::String&)override{}
    bool isBusesLayoutSupported(const BusesLayout& l)const override{return l.getMainOutputChannelSet()==juce::AudioChannelSet::stereo();}
    void prepareToPlay(double rate,int)override{
        for(auto&b:backends)b->prepare(rate);uiQueue.read(uiQueue.getNumReady());for(auto& key:hostKeyChannels)key.store(0,std::memory_order_relaxed);midiArt=-1;pendingArt=-1;observedHostArt=int(state.getRawParameterValue("articulation")->load());fade=0;fadeLength=std::max(1,int(rate*.01));
        active=std::clamp(int(state.getRawParameterValue("model")->load()),0,modelCount-1);weights.fill(0);weights[size_t(active)]=1;
        gain.reset(rate,.02);gain.setCurrentAndTargetValue(state.getRawParameterValue("gain")->load());
        effects.prepare(rate);effects.update(state,120,true);
    }
    void releaseResources()override{for(auto&b:backends)b->reset();effects.reset();}
    void processBlock(juce::AudioBuffer<float>& audio,juce::MidiBuffer& midi)override{
        juce::ScopedNoDenormals denormals;audio.clear();
        int selected=std::clamp(int(state.getRawParameterValue("model")->load()),0,modelCount-1);
        if(selected!=active){
            midiArt=-1;pendingArt=-1;
            if(weights[size_t(selected)]==0)backends[size_t(selected)]->reset();
            fadeStart=weights;
            active=selected;
            fade=fadeLength;
        }
        if(midiArt.load()>=0&&syncedRevision.load()==midiRevision.load()){midiArt=-1;observedHostArt=int(state.getRawParameterValue("articulation")->load());}
        const int hostArt=int(state.getRawParameterValue("articulation")->load());
        if(hostArt!=observedHostArt.load()){observedHostArt=hostArt;midiArt=-1;pendingArt=-1;}
        int art=std::clamp(midiArt.load()>=0?midiArt.load():hostArt,0,int(modelRegistry()[size_t(active)].articulations.size())-1);
        gain.setTargetValue(state.getRawParameterValue("gain")->load());
        const bool env=state.getRawParameterValue("envEnabled")->load()>.5f;
        double attack=env?state.getRawParameterValue("attack")->load()*.001:0,decay=env?state.getRawParameterValue("decay")->load():1,damping=env?state.getRawParameterValue("damping")->load()*.01:0;
        backends[size_t(active)]->envelope(attack,decay,damping);
        if(fade>0)for(size_t i=0;i<weights.size();++i)
            if(int(i)!=active&&weights[i]>0)backends[i]->envelope(attack,decay,damping);
        double bpm=120;if(auto* playhead=getPlayHead())if(auto position=playhead->getPosition())if(auto tempo=position->getBpm())if(std::isfinite(*tempo)&&*tempo>0)bpm=*tempo;
        effects.update(state,bpm);
        const bool initialise=resetFx.exchange(false);
        const bool restored=restoreFx.exchange(false);
        if(initialise||restored)effects.reset();
        if(audio.getNumSamples()==0)return;
        const int uiCount=uiQueue.getNumReady();
        if(midi.isEmpty()&&uiCount==0&&fade==0&&backends[size_t(active)]->silent()&&effects.silent()){
            gain.skip(audio.getNumSamples());outputPeak.store(0,std::memory_order_relaxed);return;
        }
        auto strike=[&](int key,float velocity){
            if(key>=keyswitchBase&&key<keyswitchBase+6){
                const int next=articulationForKeyswitch(active,key-keyswitchBase);
                if(next>=0){
                    art=next;midiArt=next;midiArtModel=active;pendingRevision=midiRevision.fetch_add(1)+1;
                    pendingExpected=hostArt;pendingModel=active;pendingArt=next;
                }
                return;
            }
            const auto& articulation=modelRegistry()[size_t(active)].articulations[size_t(art)];
            int variant=articulation.group;
            if(!articulation.variants.empty()){
                auto& rotation=rotations[size_t(variant)][size_t(key)];
                for(size_t i=0;i<articulation.variants.size();++i){
                    const int candidate=articulation.variants[(rotation+i)%articulation.variants.size()];
                    if(panKeyMap().root(candidate,key)>=0){
                        variant=candidate;rotation=unsigned((rotation+i+1)%articulation.variants.size());break;
                    }
                }
            }
            backends[size_t(active)]->strike(variant,key,velocity);
            visualHits[size_t(key)].store(velocity,std::memory_order_relaxed);
        };
        auto it=midi.begin();auto end=midi.end();
        for(int n=0;n<audio.getNumSamples();++n){
            while(it!=end&&(*it).samplePosition<=n){
                const auto event=*it;
                ++it;
                // Note and controller messages fit inline. Skip other MIDI, including SysEx.
                if(event.numBytes!=3)continue;
                const auto msg=event.getMessage();
                if(!msg.isNoteOn()&&!msg.isNoteOff()&&!msg.isAllSoundOff()&&!msg.isAllNotesOff())continue;
                const unsigned channel=1u<<(msg.getChannel()-1);
                if(msg.isNoteOn()){
                    hostKeyChannels[size_t(msg.getNoteNumber())].fetch_or(channel,std::memory_order_relaxed);
                    strike(msg.getNoteNumber(),msg.getFloatVelocity());
                }else if(msg.isNoteOff()){
                    hostKeyChannels[size_t(msg.getNoteNumber())].fetch_and(~channel,std::memory_order_relaxed);
                }else{
                    for(auto& key:hostKeyChannels)key.fetch_and(~channel,std::memory_order_relaxed);
                    if(msg.isAllSoundOff()){for(auto& backend:backends)backend->reset();effects.reset();}
                    else for(auto& backend:backends)backend->damp();
                }
            }
            if(n==0&&uiCount>0){
                const auto batch=uiQueue.read(uiCount);
                batch.forEach([&](int index){const auto& note=uiStrikes[size_t(index)];strike(note.key,note.velocity);});
            }
            std::array<double,2> s{};
            if(fade>0){
                const double oldWeight=double(fade-1)/fadeLength;
                for(size_t i=0;i<weights.size();++i){
                    if(weights[i]>0||int(i)==active){
                        const auto sample=backends[i]->tick();
                        for(size_t c=0;c<2;++c)s[c]+=sample[c]*modelOutputTrim[i]*weights[i];
                    }
                    // Store the next sample's weights, including across block boundaries.
                    weights[i]=fadeStart[i]*oldWeight+(int(i)==active?1.0-oldWeight:0.0);
                }
                if(--fade==0){weights.fill(0);weights[size_t(active)]=1;}
            }else{
                s=backends[size_t(active)]->tick();
                for(auto& sample:s)sample*=modelOutputTrim[size_t(active)];
            }
            s=effects.tick(s);auto g=gain.getNextValue();for(int c=0;c<std::min(2,audio.getNumChannels());++c)audio.setSample(c,n,float(std::tanh(s[size_t(c)]*g*2.)));
        }outputPeak.store(audio.getMagnitude(0,audio.getNumSamples()),std::memory_order_relaxed);midi.clear();
    }
    bool hasEditor()const override{return true;}juce::AudioProcessorEditor* createEditor()override;
    static void setSavedParameter(juce::ValueTree tree,const char* id,int value){
        for(auto child:tree)if(child.getProperty("id").toString()==id){child.setProperty("value",value,nullptr);return;}
    }
    void getStateInformation(juce::MemoryBlock& block)override{
        auto tree=state.copyState();
        const int modelIndex=std::clamp(int(state.getRawParameterValue("model")->load()),0,modelCount-1);
        const auto& descriptor=modelRegistry()[size_t(modelIndex)];
        const int art=displayedArticulation(modelIndex);
        tree.setProperty("schemaVersion",1,nullptr);
        tree.setProperty("modelId",descriptor.id,nullptr);
        if(art>=0&&art<int(descriptor.articulations.size()))
            tree.setProperty("articulationGroup",descriptor.articulations[size_t(art)].group,nullptr);
        else tree.removeProperty("articulationGroup",nullptr);
        setSavedParameter(tree,"articulation",art);
        setSavedParameter(tree,"grainFreeze",0);
        auto xml=tree.createXml();copyXmlToBinary(*xml,block);
    }
    void setStateInformation(const void* data,int size)override{
        auto xml=getXmlFromBinary(data,size);if(!xml||!xml->hasTagName(state.state.getType()))return;
        auto tree=juce::ValueTree::fromXml(*xml);
        const auto modelId=tree.getProperty("modelId").toString();
        const int modelIndex=modelIndexForID(modelId.toRawUTF8());
        if(modelIndex>=0){
            setSavedParameter(tree,"model",modelIndex);
            if(tree.hasProperty("articulationGroup")){
                const int group=int(tree.getProperty("articulationGroup"));
                const auto& arts=modelRegistry()[size_t(modelIndex)].articulations;
                for(size_t i=0;i<arts.size();++i)if(arts[i].group==group){
                    setSavedParameter(tree,"articulation",int(i));break;
                }
            }
        }
        setSavedParameter(tree,"grainFreeze",0);
        midiArt=-1;pendingArt=-1;
        state.replaceState(tree);
        restoreFx=true;
    }
};

class InstrumentKeyboard final:public juce::MidiKeyboardComponent,public juce::SettableTooltipClient {
    const std::array<std::atomic<unsigned>,128>& hostKeys;
public:
    int mappedGroup=-1;
    bool warmMode=false;
    int articulationCount=0,selectedKeyswitch=keyswitchBase;
    bool isKeyswitch(int key)const{return key>=keyswitchBase&&key<keyswitchBase+articulationCount;}
    bool playable(int key)const{if(mappedGroup>=0)return panKeyMap().root(mappedGroup,key)>=0;return warmMode&&key>=50&&key<=72;}
    explicit InstrumentKeyboard(Processor& processor):MidiKeyboardComponent(processor.keyboard,horizontalKeyboard),hostKeys(processor.hostKeyChannels){}
    void overlay(int note,juce::Graphics& g,juce::Rectangle<float> area){
        if(isKeyswitch(note)){g.setColour((note==selectedKeyswitch?juce::Colour(0xffd6c49a):juce::Colour(0xff5299bd)).withAlpha(.65f));g.fillRect(area);g.setColour(juce::Colours::black);g.setFont(11.f);g.drawText(juce::String(note-keyswitchBase+1),area.toNearestInt(),juce::Justification::centred);return;}
        if(!playable(note)){g.setColour(juce::Colours::black.withAlpha(.65f));g.fillRect(area);}
    }
    void drawWhiteNote(int n,juce::Graphics&g,juce::Rectangle<float>a,bool down,bool over,juce::Colour line,juce::Colour text)override{MidiKeyboardComponent::drawWhiteNote(n,g,a,down||hostKeys[size_t(n)].load(std::memory_order_relaxed)!=0,over,line,text);overlay(n,g,a);}
    void drawBlackNote(int n,juce::Graphics&g,juce::Rectangle<float>a,bool down,bool over,juce::Colour colour)override{MidiKeyboardComponent::drawBlackNote(n,g,a,down||hostKeys[size_t(n)].load(std::memory_order_relaxed)!=0,over,colour);overlay(n,g,a);}
    bool mouseDownOnKey(int n,const juce::MouseEvent&)override{return isKeyswitch(n)||playable(n);}
    bool mouseDraggedToKey(int n,const juce::MouseEvent&)override{return isKeyswitch(n)||playable(n);}
};
#include "InstrumentEditor.h"
juce::AudioProcessorEditor* Processor::createEditor(){return new Editor(*this);}
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter(){return new Processor();}

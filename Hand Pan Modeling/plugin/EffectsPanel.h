#pragma once
#include "SeriesLook.h"
#include "SoundControls.h"

class PowerButton final:public juce::Button {
    SeriesLook& look;
public:
    PowerButton(const juce::String& name,SeriesLook& style):Button(name+" power"),look(style){setClickingTogglesState(true);setTooltip("Enable / bypass "+name+". Lit = enabled.");}
    void paintButton(juce::Graphics& g,bool over,bool down)override{
        const auto b=getLocalBounds().toFloat().reduced(5);const auto c=b.getCentre();float radius=std::min(b.getWidth(),b.getHeight())*.37f;
        if(over||down){g.setColour(look.line);g.fillRoundedRectangle(getLocalBounds().toFloat().reduced(1),2);}
        g.setColour(getToggleState()?look.accent:look.dim.withAlpha(.55f));
        juce::Path ring;ring.addCentredArc(c.x,c.y+1,radius,radius,0,.7f,juce::MathConstants<float>::twoPi-.7f,true);
        g.strokePath(ring,juce::PathStrokeType(1.8f,juce::PathStrokeType::curved,juce::PathStrokeType::rounded));
        g.drawLine(c.x,c.y-radius-1,c.x,c.y+1,1.8f);
    }
};
class SoundSlider final:public juce::Component {
    SeriesLook& look;
    const SoundControl& spec;
    bool rotary=false;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
public:
    juce::Slider slider;
    SoundSlider(juce::AudioProcessorValueTreeState& state,const char* id,SeriesLook& style,bool knob=false):look(style),spec(soundControl(id)),rotary(knob){
        setComponentID(id);addAndMakeVisible(slider);slider.setComponentID(juce::String(id)+"Slider");slider.setName(spec.name);
        slider.setSliderStyle(rotary?juce::Slider::RotaryHorizontalVerticalDrag:juce::Slider::LinearHorizontal);slider.setTextBoxStyle(rotary?juce::Slider::TextBoxBelow:juce::Slider::TextBoxRight,false,rotary?78:57,23);slider.setRotaryParameters(juce::MathConstants<float>::pi*1.25f,juce::MathConstants<float>::pi*2.75f,true);slider.setMouseDragSensitivity(180);
        attachment=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(state,id,slider);
        slider.textFromValueFunction=[this](double v){return juce::String(v,juce::String(spec.unit)=="x"?2:juce::String(spec.unit)=="dB"?1:0)+" "+spec.unit;};
        if(spec.low<0)slider.getProperties().set("bipolarZero",slider.valueToProportionOfLength(0));
        slider.valueFromTextFunction=[](const juce::String& value){return value.getDoubleValue();};slider.updateText();
        slider.setTooltip(juce::String(spec.name)+" (double-click to reset)");slider.setDoubleClickReturnValue(true,spec.initial);
        if(juce::String(id)=="attack")slider.setTooltip("Soften each new strike. Existing ringing notes continue; Warm uses a longer excitation contact.");
        if(juce::String(id)=="decay")slider.setTooltip("Scale the source vibration decay. 1.00 x retains its natural decay.");
        if(juce::String(id)=="damping")slider.setTooltip("Add contact damping, absorbing higher partials more strongly.");
    }
    void paint(juce::Graphics& g)override{g.setColour(isEnabled()?look.dim:look.dim.withAlpha(.4f));g.setFont(juce::Font(juce::FontOptions(look.calibre?"Bahnschrift":"Segoe UI",10.f,juce::Font::plain)));g.drawText(juce::String(spec.name).toUpperCase(),2,0,getWidth()-4,17,rotary?juce::Justification::centred:juce::Justification::centredLeft);}
    ~SoundSlider()override{attachment.reset();}
    void resized()override{slider.setBounds(0,18,getWidth(),getHeight()-18);}
};
struct EffectModuleSpec {const char* name;const char* enabled;std::vector<const char*> controls;};
inline const std::array<EffectModuleSpec,6>& effectModules(){
    static const std::array<EffectModuleSpec,6> specs{{
        {"GAIN EQ","gainEqEnabled",{"gainLow","gainMid","gainHigh"}},
        {"CUT / SHELF","eqEnabled",{"lowCut","highCut","eqLow","eqHigh"}},
        {"COMPRESS","compressEnabled",{"compressThreshold","compressRatio","compressAttack","compressRelease","compressMakeup","compressMix"}},
        {"GRANULAR","grainEnabled",{"grainMix","grainSize","grainDensity","grainPitch","grainPosition","grainSpray"}},
        {"DELAY","delayEnabled",{"delaySend","delayTime","delayFeedback","delayTone"}},
        {"REVERB","reverbEnabled",{"reverbSend","reverbPredelay","reverbSize","reverbTone","reverbWidth"}}
    }};return specs;
}
class EffectModule final:public juce::Component {
    juce::AudioProcessorValueTreeState& state;SeriesLook& look;const EffectModuleSpec& spec;
    std::vector<std::unique_ptr<SoundSlider>> controls;
    PowerButton power;
    juce::TextButton sync{"SYNC"},reverse{"REVERSE"},freeze{"FREEZE"};juce::ComboBox division;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>> attachments;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> divisionAttachment;
public:
    std::function<float()> readReduction;
    EffectModule(juce::AudioProcessorValueTreeState& s,SeriesLook& style,int index):state(s),look(style),spec(effectModules()[size_t(index)]),power(spec.name,look){
        setComponentID("fxModule"+juce::String(index));addAndMakeVisible(power);power.setComponentID(spec.enabled);attachments.push_back(std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(state,spec.enabled,power));
        for(auto id:spec.controls){auto c=std::make_unique<SoundSlider>(state,id,look,true);addAndMakeVisible(*c);controls.push_back(std::move(c));}
        auto toggle=[&](juce::TextButton& b,const char* id){b.setClickingTogglesState(true);b.setComponentID(id);addAndMakeVisible(b);attachments.push_back(std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(state,id,b));};
        if(juce::String(spec.enabled)=="delayEnabled"){
            toggle(sync,"delaySync");sync.setTooltip("Sync to host tempo. Without host tempo: 120 BPM.");addAndMakeVisible(division);division.setComponentID("delayDivision");division.addItemList({"1/16","1/8","1/8 dotted","1/4","1/4 dotted","1/2","1/1"},1);divisionAttachment=std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(state,"delayDivision",division);
        }
        if(juce::String(spec.enabled)=="grainEnabled"){toggle(reverse,"grainReverse");toggle(freeze,"grainFreeze");freeze.setTooltip("Hold the live grain recording. The audio buffer is not stored in presets.");}
        updateEnabled();
    }
    void updateEnabled(){bool enabled=state.getRawParameterValue(spec.enabled)->load()>.5f;for(auto& c:controls)c->setEnabled(enabled);if(juce::String(spec.enabled)=="delayEnabled"){bool tempo=state.getRawParameterValue("delaySync")->load()>.5f;controls[1]->setEnabled(enabled&&!tempo);sync.setEnabled(enabled);division.setEnabled(enabled&&tempo);}reverse.setEnabled(enabled);freeze.setEnabled(enabled);repaint();}
    void resized()override{
        power.setBounds(getWidth()-39,5,29,29);for(size_t i=0;i<controls.size();++i)controls[i]->setBounds(9+int(i%2)*99,43+int(i/2)*90,94,86);
        sync.setBounds(12,252,59,25);division.setBounds(78,252,getWidth()-90,25);reverse.setBounds(9,317,94,23);freeze.setBounds(108,317,94,23);
    }
    void paint(juce::Graphics& g)override{
        g.fillAll(look.panel);g.setColour(look.line);g.drawRect(getLocalBounds(),1);g.drawHorizontalLine(37,9,float(getWidth()-9));g.setColour(look.ink);g.setFont(juce::Font(juce::FontOptions(look.calibre?"Bahnschrift":"Segoe UI",13.f,juce::Font::plain)));g.drawText(spec.name,12,7,getWidth()-53,25,juce::Justification::centredLeft);
        if(juce::String(spec.enabled)=="compressEnabled"){
            float reduction=readReduction?readReduction():0;g.setFont(10.f);g.setColour(look.dim);g.drawText("GR",12,317,25,16,juce::Justification::centredLeft);g.drawText(juce::String(reduction,1)+" dB",140,317,60,16,juce::Justification::centredRight);g.setColour(look.line);g.fillRect(39,322,92,5);g.setColour(look.accent);g.fillRect(39,322,int(92*std::clamp(reduction/24.f,0.f,1.f)),5);
        }
    }
};
class EffectsPanel final:public juce::Component,private juce::Timer {
    juce::AudioProcessorValueTreeState& state;SeriesLook& look;
    juce::Viewport viewport;juce::Component rack;
    std::vector<std::unique_ptr<EffectModule>> modules;
    juce::TextButton close{"CLOSE"},initialise{"INIT FX"};

public:
    std::function<void()> onClose,onInitialise;std::function<float()> readReduction;
    EffectsPanel(juce::AudioProcessorValueTreeState& s,SeriesLook& style):state(s),look(style){
        setComponentID("effectsPanel");setWantsKeyboardFocus(true);viewport.setComponentID("effectsViewport");rack.setComponentID("effectsRack");addAndMakeVisible(viewport);viewport.setViewedComponent(&rack,false);viewport.setScrollBarsShown(false,true);viewport.setScrollBarThickness(10);viewport.setScrollOnDragMode(juce::Viewport::ScrollOnDragMode::never);
        for(int i=0;i<int(effectModules().size());++i){auto module=std::make_unique<EffectModule>(state,look,i);if(i==2)module->readReduction=[this]{return readReduction?readReduction():0;};rack.addAndMakeVisible(*module);modules.push_back(std::move(module));}
        for(auto* b:{&close,&initialise})addAndMakeVisible(b);
        close.setComponentID("closeEffects");close.onClick=[this]{if(onClose)onClose();};initialise.setComponentID("initialiseEffects");initialise.setTooltip("Reset all FX, bypass every effect and clear FX tails. Keep instrument, envelope and output.");initialise.onClick=[this]{initialiseEffects(state);if(onInitialise)onInitialise();timerCallback();};
        getProperties().set("effectsModuleCount",int(modules.size()));startTimerHz(15);timerCallback();
    }
    ~EffectsPanel()override{stopTimer();viewport.setViewedComponent(nullptr,false);}
    void timerCallback()override{for(auto& module:modules)module->updateEnabled();}
    bool keyPressed(const juce::KeyPress& key)override{if(key==juce::KeyPress::escapeKey){if(onClose)onClose();return true;}return false;}
    void paint(juce::Graphics& g)override{
        g.fillAll(look.bg);g.setColour(look.line);g.drawRect(getLocalBounds(),1);g.setColour(look.ink);g.setFont(20.f);g.drawText("EFFECTS",18,13,280,28,juce::Justification::centredLeft);g.setColour(look.dim);g.setFont(10.f);g.drawText("GAIN EQ > CUT / SHELF > COMPRESS > GRANULAR > DELAY > REVERB",18,47,getWidth()-36,18,juce::Justification::centredLeft);g.drawText("Power lit = enabled  /  Double-click a knob to reset",18,getHeight()-31,getWidth()-36,20,juce::Justification::centredLeft);
    }
    void resized()override{
        close.setBounds(getWidth()-80,14,62,28);initialise.setBounds(getWidth()-170,14,80,28);viewport.setBounds(16,79,getWidth()-32,getHeight()-124);rack.setSize(int(modules.size())*220-10,std::max(344,viewport.getHeight()-12));for(size_t i=0;i<modules.size();++i)modules[i]->setBounds(int(i)*220,0,210,rack.getHeight());timerCallback();
    }
};

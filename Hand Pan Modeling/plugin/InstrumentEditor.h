#pragma once

// Native rendering of UI Design/series-ui-design.html. No browser runtime required.
#include "SeriesLook.h"
#include "EffectsPanel.h"

class Editor final:public juce::AudioProcessorEditor,private juce::Timer,private juce::ListBoxModel {
    Processor& p;
    SeriesLook look;
    juce::TooltipWindow tooltips{this,500};
    juce::ComboBox model,articulation;
    juce::Slider volume;
    SoundSlider attack,decay,damping,delaySend,reverbSend;
    PowerButton envPower,delayPower,reverbPower;
    juce::TextButton fx{"FX"};
    EffectsPanel effectsPanel;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>> powerAttachments;
    std::vector<int> visibleArts;
    juce::TextButton keyboardLeft{"<"},keyboardFit{"Fit"},keyboardRight{">"};
    InstrumentKeyboard keys;
    juce::Label detail;
    juce::ListBox arts{"Articulations",this};
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> gainAttachment;
    int displayed=-1,displayedGroup=-1,selectedArt=0,displayedArt=-1,held=-1;
    float meter=0;
    bool syncing=false;
    int viewLow=0,viewHigh=96,supportedLow=0,supportedHigh=96;
    std::array<float,128> envelopes{};
    struct Zone {int note;juce::Path path;juce::Point<float> centre;};
    std::vector<Zone> zones;
    const juce::Rectangle<int> stage{250,60,530,480};
    void layoutKeyboard(){
        keys.setAvailableRange(viewLow,viewHigh);keys.setLowestVisibleKey(viewLow);
        // Measure in units of white-key width so black endpoints also fit precisely.
        keys.setKeyWidth(1.f);
        float right=0;for(int key=viewLow;key<=viewHigh;++key)right=std::max(right,keys.getRectangleForKey(key).getRight());
        keys.setKeyWidth(float(keys.getWidth())/std::max(1.f,right));
        keys.setLowestVisibleKey(viewLow);keys.repaint();
        getProperties().set("keyboardViewLow",viewLow);getProperties().set("keyboardViewHigh",viewHigh);
        keyboardLeft.setEnabled(viewLow>0);keyboardRight.setEnabled(viewHigh<127);
    }
    void fitKeyboard(){
        // Leave two octaves below the sound range for future keyswitch assignments.
        viewLow=std::min(keyswitchBase,std::max(0,supportedLow-24));viewHigh=supportedHigh;
        if(juce::MidiMessage::isMidiNoteBlack(viewLow)&&viewLow>0)--viewLow;
        layoutKeyboard();
    }
    void moveKeyboard(int delta){
        delta=std::clamp(delta,-viewLow,127-viewHigh);viewLow+=delta;viewHigh+=delta;layoutKeyboard();
    }
    void change(const char* id,float value){auto* par=p.state.getParameter(id);par->beginChangeGesture();par->setValueNotifyingHost(par->convertTo0to1(value));par->endChangeGesture();}
    void text(juce::Graphics& g,juce::String s,int x,int y,int w,int h,float size,juce::Colour c,juce::Justification j=juce::Justification::centredLeft){g.setColour(c);g.setFont(juce::Font(juce::FontOptions(look.calibre?"Bahnschrift":"Segoe UI",size,juce::Font::plain)));g.drawText(s,x,y,w,h,j);}
    void buildZones(){
        zones.clear();
        // Fixed original design. Keyboard/MIDI extension never creates tone fields.
        const std::array<std::vector<int>,7> surfaces{{
            {53,57,58,60,64,65,69,70,72},{50,57,58,60,62,64,65,69,-1},
            {47,52,54,55,59,60,63,64,66},{48,51,55,56,60,62,63,67,-1},
            {50,57,60,62,64,65,67,69,72},{48,52,53,55,57,59,60,64},
            {50,57,58,60,62,64,65,69,72}}};
        const auto& notes=surfaces[size_t(displayed)];
        getProperties().set("stageZoneCount",int(notes.size()));
        for(size_t i=0;i<notes.size();++i){
            const bool centre=!look.calibre&&notes.size()==9&&i==0;
            const float a=float(i-(notes.size()==9&&!look.calibre?1:0))*juce::MathConstants<float>::twoPi/float(notes.size()==9&&!look.calibre?8:notes.size())-juce::MathConstants<float>::halfPi;
            const float r=centre?0.f: (notes.size()>9?163.f:142.f);
            juce::Point<float> c{515+std::cos(a)*r,286+std::sin(a)*r};
            juce::Path path;
            if(look.calibre){path.addRoundedRectangle(-52,-19,104,38,18);path.applyTransform(juce::AffineTransform::rotation(a).translated(c.x,c.y));}
            else{float rx=centre?54.f:(notes.size()>9?19.f:36.f);path.addEllipse(-rx,-rx*.8f,rx*2,rx*1.6f);path.applyTransform(juce::AffineTransform::rotation(a+juce::MathConstants<float>::halfPi).translated(c.x,c.y));}
            zones.push_back({notes[i],path,c});
        }
    }
    void rebuildArts(){
        visibleArts.clear();if(displayed<0)return;
        for(int i=0;i<int(modelRegistry()[size_t(displayed)].articulations.size());++i)visibleArts.push_back(i);
        arts.updateContent();getProperties().set("visibleArticulationCount",int(visibleArts.size()));
        auto it=std::find(visibleArts.begin(),visibleArts.end(),selectedArt);if(it!=visibleArts.end())arts.selectRow(int(it-visibleArts.begin()),true,false);else arts.deselectAllRows();
    }
    int getNumRows()override{return int(visibleArts.size());}
    void paintListBoxItem(int row,juce::Graphics& g,int w,int h,bool)override{
        if(row<0||row>=getNumRows())return;int art=visibleArts[size_t(row)];bool selected=art==selectedArt;
        if(selected){g.setColour(look.calibre?look.accent:look.line);g.fillRoundedRectangle(0,0,float(w),float(h-2),look.calibre?0.f:3.f);}
        if(look.calibre){g.setColour(look.line);g.drawHorizontalLine(h-1,0,float(w));}
        text(g,modelRegistry()[size_t(displayed)].articulations[size_t(art)].name,10,0,w-60,h,13,selected?look.ink:look.dim);
        text(g,juce::MidiMessage::getMidiNoteName(keyswitchForArticulation(displayed,art),true,true,4),w-49,0,44,h,11,look.secondary,juce::Justification::centredRight);
    }
    void selectArtRow(int row){if(row>=0&&row<getNumRows())change("articulation",float(visibleArts[size_t(row)]));}
    void listBoxItemClicked(int row,const juce::MouseEvent&)override{selectArtRow(row);}
    void returnKeyPressed(int row)override{selectArtRow(row);}
    void selectedRowsChanged(int row)override{if(!syncing&&row>=0&&row<getNumRows()&&visibleArts[size_t(row)]!=selectedArt)selectArtRow(row);}
public:
    Editor(Processor& proc):AudioProcessorEditor(proc),p(proc),attack(p.state,"attack",look),decay(p.state,"decay",look),damping(p.state,"damping",look),delaySend(p.state,"delaySend",look,true),reverbSend(p.state,"reverbSend",look,true),envPower("Envelope",look),delayPower("Delay",look),reverbPower("Reverb",look),effectsPanel(p.state,look),keys(p){
        look.theme(false);setLookAndFeel(&look);
        for(auto* c:std::initializer_list<juce::Component*>{&model,&articulation,&volume,&keys,&detail,&arts,&keyboardLeft,&keyboardFit,&keyboardRight})addAndMakeVisible(c);
        for(auto* c:std::initializer_list<juce::Component*>{&attack,&decay,&damping,&delaySend,&reverbSend,&envPower,&delayPower,&reverbPower,&fx})addAndMakeVisible(c);
        addChildComponent(effectsPanel);effectsPanel.readReduction=[this]{return p.effects.gainReduction();};effectsPanel.onInitialise=[this]{p.resetFx=true;};effectsPanel.onClose=[this]{effectsPanel.setVisible(false);fx.setToggleState(false,juce::dontSendNotification);};
        fx.setComponentID("effectsToggle");fx.setClickingTogglesState(true);fx.setTooltip("Open the effects channel strip");fx.onClick=[this]{effectsPanel.setVisible(fx.getToggleState());if(effectsPanel.isVisible()){effectsPanel.toFront(false);effectsPanel.grabKeyboardFocus();}};
        for(auto pair:std::initializer_list<std::pair<const char*,juce::Button*>>{{"envEnabled",&envPower},{"delayEnabled",&delayPower},{"reverbEnabled",&reverbPower}}){pair.second->setComponentID(pair.first);powerAttachments.push_back(std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.state,pair.first,*pair.second));}
        model.setComponentID("instrumentSelector");articulation.setComponentID("articulationSelector");keys.setComponentID("instrumentKeyboard");
        for(auto family:{InstrumentFamily::Handpan,InstrumentFamily::TongueDrum}){model.addSectionHeading(family==InstrumentFamily::Handpan?"HANDPANS":"TONGUE DRUMS");for(int i=0;i<modelCount;++i)if(modelRegistry()[size_t(i)].family==family)model.addItem(modelRegistry()[size_t(i)].name,i+1);}
        model.onChange=[this]{if(model.getSelectedId()>0){change("model",float(model.getSelectedId()-1));timerCallback();}};
        articulation.onChange=[this]{if(articulation.getSelectedId()>0)change("articulation",float(articulation.getSelectedId()-1));};
        for(auto* s:{&volume}){s->setSliderStyle(juce::Slider::LinearHorizontal);s->setTextBoxStyle(juce::Slider::TextBoxRight,false,44,24);s->setNumDecimalPlacesToDisplay(2);s->textFromValueFunction=[](double v){return juce::String(v,2);};}
        gainAttachment=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.state,"gain",volume);
        for(auto* s:{&volume}){s->textFromValueFunction=[](double v){return juce::String(v,2);};s->updateText();}
        arts.setRowHeight(29);arts.setOutlineThickness(0);
        detail.setFont(juce::Font(juce::FontOptions(11.f)));detail.setMinimumHorizontalScale(.7f);
        keys.setOctaveForMiddleC(4);
        keys.setScrollButtonsVisible(false);
        keyboardLeft.setComponentID("keyboardOctaveDown");keyboardFit.setComponentID("keyboardFit");keyboardRight.setComponentID("keyboardOctaveUp");
        keyboardLeft.setTooltip("Show one octave lower (does not transpose sound)");
        keyboardRight.setTooltip("Show one octave higher (does not transpose sound)");
        keyboardFit.setTooltip("Fit playable range and keyswitches");
        keyboardLeft.onClick=[this]{moveKeyboard(-12);};keyboardRight.onClick=[this]{moveKeyboard(12);};keyboardFit.onClick=[this]{fitKeyboard();};
        setResizable(true,false);setResizeLimits(800,512,2400,1536);setSize(1000,640);timerCallback();startTimerHz(30);
    }
    ~Editor()override{stopTimer();if(held>=0)p.keyboard.noteOff(1,held,0);setLookAndFeel(nullptr);}
    void timerCallback()override{
        juce::ScopedValueSetter<bool> updating(syncing,true);
        int m=std::clamp(int(p.state.getRawParameterValue("model")->load()),0,modelCount-1);
        if(m!=displayed){
            // Only reflect actual host changes. Reasserting the old value every frame
            // can cancel ComboBox's pending asynchronous user selection notification.
            model.setSelectedId(m+1,juce::dontSendNotification);
            if(held>=0){p.keyboard.noteOff(1,held,0);held=-1;}
            displayed=m;displayedGroup=-1;displayedArt=-1;envelopes.fill(0);for(auto& hit:p.visualHits)hit.exchange(0);
            look.theme(modelRegistry()[size_t(m)].family==InstrumentFamily::TongueDrum);
            getProperties().set("theme",look.calibre?"calibre":"nodal");
            articulation.clear(juce::dontSendNotification);
            for(int i=0;i<int(modelRegistry()[size_t(m)].articulations.size());++i)articulation.addItem(modelRegistry()[size_t(m)].articulations[size_t(i)].name,i+1);
            keys.articulationCount=int(modelRegistry()[size_t(m)].articulations.size());
            juce::String tips="Keyswitches: ";for(int i=0;i<keys.articulationCount;++i)tips+=juce::MidiMessage::getMidiNoteName(keyswitchForArticulation(m,i),true,true,4)+" "+modelRegistry()[size_t(m)].articulations[size_t(i)].name+"; ";keys.setTooltip(tips);
            keys.warmMode=m==6;
            keys.setColour(juce::MidiKeyboardComponent::whiteNoteColourId,juce::Colour(look.calibre?0xffffffff:0xff5a6670));
            keys.setColour(juce::MidiKeyboardComponent::blackNoteColourId,juce::Colour(look.calibre?0xff16191b:0xff1a2026));
            keys.setColour(juce::MidiKeyboardComponent::keyDownOverlayColourId,look.accent.withAlpha(.8f));
            keys.setColour(juce::MidiKeyboardComponent::textLabelColourId,look.ink);
            sendLookAndFeelChange();rebuildArts();resized();
        }
        selectedArt=std::clamp(p.displayedArticulation(m),0,articulation.getNumItems()-1);
        if(selectedArt!=displayedArt){displayedArt=selectedArt;articulation.setSelectedId(selectedArt+1,juce::dontSendNotification);rebuildArts();}
        keys.selectedKeyswitch=keyswitchForArticulation(m,selectedArt);getProperties().set("activeArticulation",selectedArt);getProperties().set("activeKeyswitch",keys.selectedKeyswitch);keys.repaint();
        int group=modelRegistry()[size_t(m)].articulations[size_t(selectedArt)].group;
        if(group!=displayedGroup){
            displayedGroup=group;keys.mappedGroup=m<6?group:-1;
            supportedLow=m==6?50:127;supportedHigh=m==6?72:0;
            if(m<6)for(auto z:panKeyMap().groups[size_t(group)]){supportedLow=std::min(supportedLow,z.low);supportedHigh=std::max(supportedHigh,z.high);}
            fitKeyboard();buildZones();
        }
        const juce::String status=modelRegistry()[size_t(m)].family==InstrumentFamily::TongueDrum?"Tongue drum":"Handpan";
        detail.setText(juce::String(status)+"  |  View: "+juce::MidiMessage::getMidiNoteName(viewLow,true,true,4)+" - "+juce::MidiMessage::getMidiNoteName(viewHigh,true,true,4)+"  |  "+(p.getSampleRate()>0?juce::String(p.getSampleRate()/1000.,1)+" kHz":"Audio idle"),juce::dontSendNotification);
        for(size_t i=0;i<envelopes.size();++i)envelopes[i]=std::max(envelopes[i]*.94f,p.visualHits[i].exchange(0,std::memory_order_relaxed));
        meter=std::max(meter*.85f,p.outputPeak.load(std::memory_order_relaxed));
        bool env=p.state.getRawParameterValue("envEnabled")->load()>.5f;attack.setEnabled(env);decay.setEnabled(env);damping.setEnabled(env);
        delaySend.setEnabled(p.state.getRawParameterValue("delayEnabled")->load()>.5f);reverbSend.setEnabled(p.state.getRawParameterValue("reverbEnabled")->load()>.5f);
        repaint();arts.repaint();
    }
    juce::AffineTransform designTransform()const{float scale=std::min(getWidth()/1000.f,getHeight()/640.f);return juce::AffineTransform::scale(scale).translated((getWidth()-1000*scale)*.5f,(getHeight()-640*scale)*.5f);}
    void paint(juce::Graphics& g)override{
        g.fillAll(look.bg);g.addTransform(designTransform());g.setColour(look.panel);g.fillRect(0,60,250,480);g.fillRect(780,60,220,480);
        g.setColour(look.line);g.drawHorizontalLine(59,0,1000);g.drawHorizontalLine(539,0,1000);g.drawVerticalLine(250,60,540);g.drawVerticalLine(780,60,540);
        text(g,"SONFORM: Void",22,12,210,26,17.f,look.accent);
        text(g,"VERSION " JucePlugin_VersionString,22,37,210,15,10,look.dim);
        text(g,"INSTRUMENT",256,4,120,14,10,look.dim);
        text(g,"OUTPUT",797,11,90,18,11,look.dim);
        g.setColour(look.line);g.fillRect(797,34,180,look.calibre?12:5);g.setColour(look.calibre?look.ink:look.accent);g.fillRect(797,34,int(180*std::clamp(meter,0.f,1.f)),look.calibre?12:5);
        if(look.calibre){g.setColour(look.bg);for(int x=797;x<977;x+=6)g.fillRect(x,34,2,12);}
        text(g,"EXCITATION",20,76,210,28,18,look.ink);text(g,"ARTICULATION",20,110,210,22,10,look.dim);
        text(g,"SOUND",800,76,180,28,18,look.ink);text(g,"ENVELOPE",800,116,145,22,11,look.dim);
        g.setColour(look.line);g.drawHorizontalLine(284,800,980);g.drawHorizontalLine(434,800,980);
        text(g,"EFFECTS",800,287,100,22,11,look.dim);text(g,"OUTPUT",800,440,180,20,11,look.dim);
        text(g,"Click surface or play MIDI",266,70,440,20,11,look.dim);
        text(g,"PLAYABLE SURFACE",266,508,498,20,11,look.dim,juce::Justification::centred);
        g.setColour(look.line);g.drawEllipse(309,80,412,412,1);
        if(look.calibre){g.setColour(look.line);g.drawLine(294,286,736,286);g.drawLine(515,65,515,507);for(int i=0;i<24;++i){float a=i*juce::MathConstants<float>::twoPi/24;g.drawLine(515+std::cos(a)*199,286+std::sin(a)*199,515+std::cos(a)*206,286+std::sin(a)*206);}}
        for(const auto& z:zones){
            float e=z.note>=0?envelopes[size_t(z.note)]:0;g.setColour(look.calibre?look.ink:look.line.interpolatedWith(look.accent,e));
            if(look.calibre&&e>.01f){g.setColour(look.accent.withAlpha(.25f+.75f*e));g.fillPath(z.path);g.setColour(look.ink);}
            g.strokePath(z.path,juce::PathStrokeType(1.f));
            if(!look.calibre){
                // Stable sand positions converge onto nodal rings after an actual MIDI strike.
                auto b=z.path.getBounds();juce::Random random(z.note*12345);
                g.setColour((e>.05f?look.accent:look.dim).withAlpha(.65f));
                for(int i=0;i<190;++i){juce::Point<float> h{b.getX()+random.nextFloat()*b.getWidth(),b.getY()+random.nextFloat()*b.getHeight()};if(!z.path.contains(h))continue;float a=random.nextFloat()*juce::MathConstants<float>::twoPi;auto t=z.centre+juce::Point<float>(std::cos(a)*b.getWidth()*.28f,std::sin(a)*b.getHeight()*.28f);auto q=h+(t-h)*e;g.fillRect(q.x,q.y,1.5f,1.5f);}
            }
            const bool calibrated=false;
            text(g,(z.note>=0?juce::MidiMessage::getMidiNoteName(z.note,true,true,4):juce::String("-")),int(z.centre.x)-24,int(z.centre.y)-9,48,18,11,calibrated?look.secondary:look.ink,juce::Justification::centred);
            if(calibrated){g.setColour(look.secondary);g.fillEllipse(z.centre.x-2,z.centre.y+12,4,4);}
        }
    }
    void resized()override{model.setBounds(256,21,500,30);articulation.setBounds(20,136,210,29);arts.setBounds(20,174,210,180);volume.setBounds(798,461,184,28);attack.setBounds(798,145,184,42);decay.setBounds(798,191,184,42);damping.setBounds(798,237,184,42);envPower.setBounds(952,112,28,28);fx.setBounds(912,288,68,24);delaySend.setBounds(798,338,88,94);reverbSend.setBounds(892,338,88,94);delayPower.setBounds(828,309,28,28);reverbPower.setBounds(922,309,28,28);effectsPanel.setBounds(12,60,768,480);keyboardLeft.setBounds(20,550,30,18);keyboardFit.setBounds(20,570,30,18);keyboardRight.setBounds(20,590,30,18);keys.setBounds(56,550,924,58);detail.setBounds(20,613,960,22);for(auto* child:getChildren())child->setTransform(designTransform());layoutKeyboard();}
    void mouseDown(const juce::MouseEvent& e)override{
        auto point=e.position.transformedBy(designTransform().inverted());if(!stage.toFloat().contains(point))return;
        for(const auto& z:zones)if(z.path.contains(point)&&keys.playable(z.note)){held=z.note;p.keyboard.noteOn(1,held,.8f);envelopes[size_t(held)]=.8f;break;}
    }
    void mouseUp(const juce::MouseEvent&)override{if(held>=0){p.keyboard.noteOff(1,held,0);held=-1;}}
};

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_audio_plugin_client/detail/juce_CreatePluginFilter.h>
#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>
#include "SeriesLook.h"

class StudioSettings final:public juce::Component {
    SeriesLook& look;
    juce::Viewport viewport;
    juce::TextButton save{"Save preset"},load{"Load preset"},reset{"Reset sound"};
public:
    StudioSettings(juce::StandalonePluginHolder& holder,SeriesLook& style):look(style){
        setLookAndFeel(&look);addAndMakeVisible(viewport);
        auto* selector=new juce::AudioDeviceSelectorComponent(holder.deviceManager,0,0,2,2,true,false,true,false);
        selector->setSize(520,600);viewport.setViewedComponent(selector,true);viewport.setScrollBarsShown(true,false);setSize(600,640);
        for(auto* button:{&save,&load,&reset})addAndMakeVisible(button);
        save.onClick=[&holder]{holder.askUserToSaveState();};load.onClick=[&holder]{holder.askUserToLoadState();};
        reset.onClick=[&holder]{for(auto* parameter:holder.processor->getParameters()){parameter->beginChangeGesture();parameter->setValueNotifyingHost(parameter->getDefaultValue());parameter->endChangeGesture();}};
    }
    ~StudioSettings()override{setLookAndFeel(nullptr);}
    void paint(juce::Graphics& g)override{g.fillAll(look.bg);g.setColour(look.accent);g.setFont(22.f);g.drawText("AUDIO / MIDI",24,18,500,30,juce::Justification::centredLeft);g.setColour(look.dim);g.setFont(12.f);g.drawText("Device, sample rate, buffer and MIDI inputs",24,49,540,22,juce::Justification::centredLeft);}
    void resized()override{viewport.setBounds(20,82,getWidth()-40,getHeight()-158);if(auto* c=viewport.getViewedComponent())c->setSize(viewport.getWidth()-16,std::max(600,c->getHeight()));int width=(getWidth()-60)/3;save.setBounds(20,getHeight()-54,width,34);load.setBounds(30+width,getHeight()-54,width,34);reset.setBounds(40+2*width,getHeight()-54,width,34);}
};
class SettingsWindow final:public juce::DocumentWindow {
public:
    SettingsWindow(juce::StandalonePluginHolder& holder,SeriesLook& look,juce::Component* owner):DocumentWindow("Audio / MIDI",look.bg,closeButton){setUsingNativeTitleBar(false);setLookAndFeel(&look);setTitleBarHeight(44);setContentOwned(new StudioSettings(holder,look),true);setResizable(false,false);centreAroundComponent(owner,getWidth(),getHeight());}
    ~SettingsWindow()override{clearContentComponent();setLookAndFeel(nullptr);}
    void closeButtonPressed()override{setVisible(false);}
};
class StudioWindow final:public juce::DocumentWindow,private juce::Timer {
    SeriesLook look;
    std::unique_ptr<juce::StandalonePluginHolder> holder;
    juce::TextButton settings{"Settings"};
    std::unique_ptr<SettingsWindow> settingsWindow;
    bool light=false;
public:
    explicit StudioWindow(juce::PropertySet* properties):DocumentWindow("SONFORM: Void",juce::Colour(0xff222a31),allButtons){
        look.theme(false);setUsingNativeTitleBar(false);setLookAndFeel(&look);setTitleBarHeight(44);
        holder=std::make_unique<juce::StandalonePluginHolder>(properties,false);
        setContentOwned(holder->processor->createEditorIfNeeded(),true);setResizable(true,false);setResizeLimits(820,570,2440,1640);
        addAndMakeVisible(settings);settings.onClick=[this]{if(!settingsWindow)settingsWindow=std::make_unique<SettingsWindow>(*holder,look,this);settingsWindow->setVisible(true);settingsWindow->toFront(true);};
        centreWithSize(getWidth(),getHeight());timerCallback();startTimerHz(10);setVisible(true);
    }
    ~StudioWindow()override{stopTimer();settingsWindow=nullptr;holder->savePluginState();holder->stopPlaying();clearContentComponent();setLookAndFeel(nullptr);}
    void resized()override{DocumentWindow::resized();settings.setBounds(12,8,96,28);}
    void closeButtonPressed()override{juce::JUCEApplicationBase::quit();}
    void timerCallback()override{
        auto* parameter=dynamic_cast<juce::AudioParameterChoice*>(holder->processor->getParameters()[0]);bool next=parameter&&parameter->getIndex()==5;
        if(next!=light){light=next;look.theme(light);sendLookAndFeelChange();if(settingsWindow)settingsWindow->sendLookAndFeelChange();repaint();}
    }
};
class StudioApp final:public juce::JUCEApplication {
    juce::ApplicationProperties properties;
    std::unique_ptr<StudioWindow> window;
public:
    const juce::String getApplicationName()override{return "SONFORM: Void";}
    const juce::String getApplicationVersion()override{return JucePlugin_VersionString;}
    void initialise(const juce::String&)override{juce::PropertiesFile::Options options;options.applicationName="SONFORM Void";options.filenameSuffix=".settings";options.osxLibrarySubFolder="Application Support";properties.setStorageParameters(options);window=std::make_unique<StudioWindow>(properties.getUserSettings());}
    void shutdown()override{window=nullptr;properties.saveIfNeeded();}
    void systemRequestedQuit()override{quit();}
};
juce::JUCEApplicationBase* juce_CreateApplication(){return new StudioApp;}

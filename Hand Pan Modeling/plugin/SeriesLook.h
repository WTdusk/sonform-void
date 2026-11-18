#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
class StudioWindowButton final:public juce::Button {
    int type;
public:
    explicit StudioWindowButton(int kind):Button(kind==juce::DocumentWindow::closeButton?"Close":kind==juce::DocumentWindow::minimiseButton?"Minimize":"Maximize"),type(kind){setTooltip(getName());}
    void paintButton(juce::Graphics& g,bool over,bool down)override{
        auto colour=juce::Colour(type==juce::DocumentWindow::closeButton?0xffe06464:type==juce::DocumentWindow::minimiseButton?0xffd8a84b:0xff68b18a);
        auto background=findColour(juce::ResizableWindow::backgroundColourId);g.fillAll(over?background.interpolatedWith(colour,down?.3f:.14f):background);
        float size=std::min(getWidth(),getHeight())*.46f;auto box=juce::Rectangle<float>(size,size).withCentre(getLocalBounds().toFloat().getCentre());g.setColour(colour);
        if(type==juce::DocumentWindow::closeButton){g.drawLine(box.getX(),box.getY(),box.getRight(),box.getBottom(),2.2f);g.drawLine(box.getRight(),box.getY(),box.getX(),box.getBottom(),2.2f);}
        else if(type==juce::DocumentWindow::minimiseButton)g.drawLine(box.getX(),box.getCentreY()+size*.2f,box.getRight(),box.getCentreY()+size*.2f,2.2f);
        else {g.drawRect(box,2.f);if(getToggleState())g.drawRect(box.translated(-3,3),1.5f);}
    }
};
struct SeriesLook final : juce::LookAndFeel_V4 {
    bool calibre=false;
    juce::Colour bg, panel, ink, dim, line, accent, secondary;
    void theme(bool light) {
        calibre=light;
        bg=juce::Colour(light?0xffeceeec:0xff222a31);
        panel=juce::Colour(light?0xfff7f8f7:0xff29323a);
        ink=juce::Colour(light?0xff16191b:0xffece5d5);
        dim=juce::Colour(light?0xff6e767b:0xff8a949c);
        line=juce::Colour(light?0xffc3c9cc:0xff3a454f);
        accent=juce::Colour(light?0xfff2b01e:0xffd6c49a);
        secondary=juce::Colour(light?0xff2d63a8:0xff86afc6);
        setColour(juce::Label::textColourId,ink);
        setColour(juce::ComboBox::backgroundColourId,panel);
        setColour(juce::ComboBox::textColourId,ink);
        setColour(juce::ComboBox::outlineColourId,line);
        setColour(juce::ComboBox::arrowColourId,dim);
        setColour(juce::PopupMenu::backgroundColourId,panel);
        setColour(juce::PopupMenu::textColourId,ink);
        setColour(juce::PopupMenu::highlightedBackgroundColourId,accent);
        setColour(juce::PopupMenu::highlightedTextColourId,bg);
        setColour(juce::Slider::textBoxTextColourId,ink);
        setColour(juce::Slider::textBoxOutlineColourId,juce::Colours::transparentBlack);
        setColour(juce::TextButton::buttonColourId,panel);
        setColour(juce::TextButton::textColourOffId,ink);
        setColour(juce::ListBox::backgroundColourId,juce::Colours::transparentBlack);
        setColour(juce::ListBox::outlineColourId,line);
        setColour(juce::ResizableWindow::backgroundColourId,bg);
        setColour(juce::TextEditor::backgroundColourId,panel);setColour(juce::TextEditor::textColourId,ink);setColour(juce::TextEditor::outlineColourId,line);setColour(juce::TextEditor::focusedOutlineColourId,accent);
        setColour(juce::ToggleButton::textColourId,ink);setColour(juce::ToggleButton::tickColourId,accent);
        setColour(juce::ScrollBar::thumbColourId,dim);setColour(juce::ScrollBar::backgroundColourId,bg);
    }
    juce::Font getTextButtonFont(juce::TextButton&,int h)override{return juce::Font(juce::FontOptions(calibre?"Bahnschrift":"Segoe UI",std::min(14.f,h*.6f),juce::Font::plain));}
    void drawButtonBackground(juce::Graphics& g,juce::Button& b,const juce::Colour&,bool over,bool down)override{g.setColour(down?accent:over||b.getToggleState()?line:panel);g.fillRoundedRectangle(b.getLocalBounds().toFloat().reduced(.5f),2);g.setColour(b.getToggleState()?accent:line);g.drawRoundedRectangle(b.getLocalBounds().toFloat().reduced(.5f),2,1);}
    void drawButtonText(juce::Graphics& g,juce::TextButton& b,bool,bool down)override{g.setColour((down&&!calibre?bg:ink).withAlpha(b.isEnabled()?1.f:.35f));g.setFont(getTextButtonFont(b,b.getHeight()));g.drawText(b.getButtonText(),b.getLocalBounds().reduced(3),juce::Justification::centred);}
    void drawComboBox(juce::Graphics& g,int w,int h,bool,int,int,int,int,juce::ComboBox&)override{g.setColour(panel);g.fillRect(0,0,w,h);g.setColour(line);g.drawRect(0,0,w,h);g.setColour(dim);juce::Path arrow;arrow.startNewSubPath(float(w-18),h*.43f);arrow.lineTo(float(w-12),h*.62f);arrow.lineTo(float(w-6),h*.43f);g.strokePath(arrow,juce::PathStrokeType(1.4f));}
    void drawToggleButton(juce::Graphics& g,juce::ToggleButton& b,bool,bool)override{auto box=juce::Rectangle<float>(2,float(b.getHeight()/2-7),14,14);g.setColour(panel);g.fillRect(box);g.setColour(b.getToggleState()?accent:line);g.drawRect(box,1);if(b.getToggleState())g.fillRect(box.reduced(3));g.setColour(ink);g.setFont(13.f);g.drawText(b.getButtonText(),22,0,b.getWidth()-24,b.getHeight(),juce::Justification::centredLeft);}
    void drawPopupMenuBackground(juce::Graphics& g,int w,int h)override{g.fillAll(panel);g.setColour(line);g.drawRect(0,0,w,h);}
    juce::Button* createDocumentWindowButton(int type)override{return new StudioWindowButton(type);}
    void drawDocumentWindowTitleBar(juce::DocumentWindow&,juce::Graphics& g,int w,int h,int,int,const juce::Image*,bool)override{g.fillAll(bg);g.setColour(line);g.drawHorizontalLine(h-1,0,float(w));g.setColour(ink);g.setFont(14.f);g.drawText("SONFORM: Void",124,0,std::max(0,w-260),h,juce::Justification::centredLeft);}
    void drawResizableFrame(juce::Graphics& g,int w,int h,const juce::BorderSize<int>&)override{g.setColour(line);g.drawRect(0,0,w,h);}
    juce::Font getLabelFont(juce::Label&)override{return juce::Font(juce::FontOptions(calibre?"Bahnschrift":"Segoe UI",13.f,juce::Font::plain));}
    void drawRotarySlider(juce::Graphics& g,int x,int y,int w,int h,float position,float start,float end,juce::Slider& slider)override{
        float radius=std::max(2.f,std::min(w,h)*.5f-5),cx=x+w*.5f,cy=y+h*.5f,angle=start+position*(end-start),alpha=slider.isEnabled()?1.f:.28f;
        g.setColour(panel.withMultipliedAlpha(alpha));g.fillEllipse(cx-radius+4,cy-radius+4,2*radius-8,2*radius-8);
        juce::Path track;track.addCentredArc(cx,cy,radius,radius,0,start,end,true);g.setColour(line.withMultipliedAlpha(alpha));g.strokePath(track,juce::PathStrokeType(2.3f));
        double zero=slider.getProperties().contains("bipolarZero")?double(slider.getProperties()["bipolarZero"]):0.;
        float anchor=start+float(zero)*(end-start);juce::Path value;value.addCentredArc(cx,cy,radius,radius,0,std::min(anchor,angle),std::max(anchor,angle),true);g.setColour(accent.withMultipliedAlpha(alpha));g.strokePath(value,juce::PathStrokeType(2.8f));
        g.setColour(ink.withMultipliedAlpha(alpha));g.drawLine(cx+std::sin(angle)*radius*.42f,cy-std::cos(angle)*radius*.42f,cx+std::sin(angle)*radius*.79f,cy-std::cos(angle)*radius*.79f,2.f);
    }
    void drawLinearSlider(juce::Graphics& g,int x,int y,int w,int h,float pos,float,float,juce::Slider::SliderStyle,juce::Slider& s)override{
        const float cy=float(y)+h*.5f;
        auto colour=[&](juce::Colour c){g.setColour(c.withMultipliedAlpha(s.isEnabled()?1.f:.3f));};
        colour(line);g.fillRect(float(x),cy-1,float(w),2.f);
        colour(accent);g.fillRect(float(x),cy-2,std::max(0.f,pos-x),calibre?6.f:3.f);
        if(calibre){colour(dim);for(int i=0;i<=10;++i)g.drawVerticalLine(x+w*i/10,cy+6,cy+10);colour(ink);g.fillRect(pos-1.5f,cy-5,3.f,18.f);}
        else{colour(accent);g.fillEllipse(pos-4,cy-4,8,8);}
    }
};


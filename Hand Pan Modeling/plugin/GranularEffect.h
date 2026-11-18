#pragma once
#include <array>
#include <vector>
#include <algorithm>
#include <cmath>
#include <cstdint>

// Fixed-capacity live granular resynthesis. Bypass preserves frozen recordings;
// ordinary reactivation invalidates old history without clearing a large buffer.
class GranularEffect {
    struct Grain {bool active=false;double position=0,speed=1;int age=0,length=1;double left=1,right=1,windowCos=1,windowSin=0,stepCos=1,stepSin=0;};
    std::array<std::vector<float>,2> recording;
    std::array<Grain,48> grains{};
    std::array<size_t,48> activeGrains{};size_t activeCount=0,validRecorded=0;
    double rate=48000,countdown=0,sizeMs=100,density=12,pitch=0,positionMs=150,spray=0;
    double normalisation=1,normalisationTarget=1,normalisationStep=.001;
    size_t writePosition=0;
    bool enabled=false,reverse=false,freeze=false;
    std::uint32_t randomState=0x913a756bu;
    double random(){randomState^=randomState<<13;randomState^=randomState>>17;randomState^=randomState<<5;return double(randomState)/4294967295.;}
    std::array<double,2> read(double position)const{
        const size_t length=recording[0].size(),a=size_t(position),b=a+1==length?0:a+1;
        const size_t newest=writePosition==0?length-1:writePosition-1;
        const size_t ageA=newest>=a?newest-a:newest+length-a,ageB=newest>=b?newest-b:newest+length-b;
        const bool validA=ageA<validRecorded,validB=ageB<validRecorded;double fraction=position-a;std::array<double,2> out{};
        for(size_t c=0;c<2;++c){double x=validA?recording[c][a]:0,y=validB?recording[c][b]:0;out[c]=float(x+fraction*(y-x));}return out;
    }
public:
    void prepare(double sampleRate){rate=sampleRate;normalisationStep=1-std::exp(-1/(rate*.03));for(auto& channel:recording)channel.assign(size_t(rate*4)+4,0.f);reset();}
    void suspend(){grains={};activeCount=0;countdown=0;}
    void resume(){suspend();if(!freeze){writePosition=0;validRecorded=0;}}
    void reset(){suspend();writePosition=0;validRecorded=0;randomState=0x913a756bu;normalisation=normalisationTarget;}
    void configure(bool on,double size,double amount,double semitones,double position,double scatter,bool backwards,bool hold){enabled=on;sizeMs=size;density=amount;pitch=semitones;positionMs=position;spray=scatter*.01;reverse=backwards;freeze=hold;normalisationTarget=1/std::max(1.,density*sizeMs*.001*.5);}
    std::array<double,2> tick(std::array<double,2> input){
        const size_t capacity=recording[0].size();
        if(!freeze){for(size_t c=0;c<2;++c)recording[c][writePosition]=float(input[c]);if(++writePosition==capacity)writePosition=0;validRecorded=std::min(validRecorded+1,capacity);}
        if(enabled&&--countdown<=0){
            countdown+=rate/density;
            for(size_t index=0;index<grains.size();++index)if(!grains[index].active){auto& grain=grains[index];
                grain={};grain.active=true;grain.length=std::max(2,int(sizeMs*.001*rate));
                double speed=std::pow(2.,pitch/12.);grain.speed=reverse?-speed:speed;
                double offset=positionMs*.001*rate+(random()*2-1)*spray*rate*.5;
                offset=std::clamp(std::max(offset,reverse?2.:grain.length*std::max(0.,speed-1)+2),2.,rate*2.5);
                grain.position=double(writePosition)-offset;if(grain.position<0)grain.position+=capacity;
                double pan=(random()*2-1)*spray*.8;grain.left=std::sqrt(1-pan);grain.right=std::sqrt(1+pan);
                const double phase=6.283185307179586/(grain.length-1);grain.stepCos=std::cos(phase);grain.stepSin=std::sin(phase);activeGrains[activeCount++]=index;break;
            }
        }
        if(!enabled)countdown=0;
        std::array<double,2> out{};
        for(size_t i=0;i<activeCount;){auto& grain=grains[activeGrains[i]];
            double window=.5-.5*grain.windowCos;auto source=read(grain.position);out[0]+=source[0]*window*grain.left;out[1]+=source[1]*window*grain.right;
            double nextCos=grain.windowCos*grain.stepCos-grain.windowSin*grain.stepSin;grain.windowSin=grain.windowSin*grain.stepCos+grain.windowCos*grain.stepSin;grain.windowCos=nextCos;
            grain.position+=grain.speed;if(grain.position<0)grain.position+=capacity;else if(grain.position>=capacity)grain.position-=capacity;
            if(++grain.age>=grain.length){grain.active=false;activeGrains[i]=activeGrains[--activeCount];}else ++i;
        }
        normalisation+=normalisationStep*(normalisationTarget-normalisation);for(auto& sample:out)sample*=normalisation;return out;
    }
};

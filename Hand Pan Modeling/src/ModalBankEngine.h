#pragma once
#include "CornerKeyMap.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace handpan {
// Shared linear modal mechanics for every measured instrument. No sample player.
// Load/prepare only on a stopped audio thread. Strike/tick allocate no memory.
class ModalBankEngine {
    struct Mode { double frequency=0,loss=0,left=0,right=0; };
    struct Anchor { double velocity=0; std::array<Mode,16> modes{}; };
    struct Model { std::string family; int group=0,root=0; std::vector<Anchor> anchors; };
    struct Osc { double re=0,im=0,c=0,s=0,left=0,right=0,loss=0,frequency=0; };
    struct Voice { std::array<Osc,16> modes{}; bool active=false; unsigned age=0; double fade=1,fadeStep=0,rise=1,riseStep=1; };
    std::vector<Model> models_;
    std::array<Voice,48> voices_{};
    std::array<Voice,48> retiring_{};
    std::array<double,89> fallbackGain_{};
    double rate_=48000;
    double attack_=0,decayScale_=1,damping_=0;
    struct Dynamics {std::string family;std::vector<int> groups;double gain=1;};
    std::vector<Dynamics> dynamics_;
public:
    // Configure off the audio thread. One fixed gain covers every calibrated
    // key/velocity of a model; other articulations use per-group headroom.
    void configureDynamics(std::string_view family,const std::vector<int>& groups){
        if(groups.empty())return;
        double bound=0;
        for(const auto&m:models_)if(m.family==family&&std::find(groups.begin(),groups.end(),m.group)!=groups.end())for(const auto&a:m.anchors){double sum=0;for(const auto&v:a.modes)sum+=std::max(v.left,v.right);bound=std::max(bound,sum);}
        auto it=std::find_if(dynamics_.begin(),dynamics_.end(),[&](const Dynamics& d){return d.family==family;});
        Dynamics entry{std::string(family),groups,bound>.65?.65/bound:1};
        if(it==dynamics_.end())dynamics_.push_back(std::move(entry));else *it=std::move(entry);
    }
    double familyGain(std::string_view family)const{for(const auto&d:dynamics_)if(d.family==family)return d.gain;return 1;}
    void load(const std::string& path) {
        std::ifstream f(path); if(!f)throw std::runtime_error("Cannot open modal bank");
        load(f);
    }
    void load(std::istream& f) {
        std::string line;std::getline(f,line);std::vector<Model> parsed;
        while(std::getline(f,line)) {
            std::istringstream row(line);std::string fam,token;std::getline(row,fam,',');std::array<double,8> v{};
            for(auto& n:v){if(!std::getline(row,token,','))throw std::runtime_error("Missing bank column"); n=std::stod(token);if(!std::isfinite(n))throw std::runtime_error("Nonfinite bank");}
            int group=int(v[0]),root=int(v[1]),mode=int(v[3]);
            if(group<0||group>88||root<0||root>127||mode<0||mode>=16||v[2]<=0||v[2]>127||v[4]<30||v[4]>6000||v[5]<.1||v[5]>100||v[6]<0||v[7]<0)throw std::runtime_error("Bank range");
            auto it=std::find_if(parsed.begin(),parsed.end(),[&](const Model& m){return m.family==fam&&m.group==group&&m.root==root;});
            if(it==parsed.end()){parsed.push_back({fam,group,root,{}});it=parsed.end()-1;}
            auto a=std::find_if(it->anchors.begin(),it->anchors.end(),[&](const Anchor& a){return a.velocity==v[2];});
            if(a==it->anchors.end()){it->anchors.push_back({v[2],{}});a=it->anchors.end()-1;}
            if(a->modes[mode].frequency!=0)throw std::runtime_error("Duplicate bank mode");
            a->modes[mode]={v[4],v[5],v[6],v[7]};
        }
        if(parsed.empty())throw std::runtime_error("Empty bank");
        for(auto& m:parsed)std::sort(m.anchors.begin(),m.anchors.end(),[](const Anchor&a,const Anchor&b){return a.velocity<b.velocity;});
        models_=std::move(parsed);dynamics_.clear();fallbackGain_.fill(1);
        std::array<double,89> bounds{};
        for(const auto& model:models_)for(const auto& anchor:model.anchors){double bound=0;for(auto m:anchor.modes)bound+=std::max(m.left,m.right);bounds[size_t(model.group)]=std::max(bounds[size_t(model.group)],bound);}
        for(size_t i=0;i<bounds.size();++i)if(bounds[i]>.4)fallbackGain_[i]=.4/bounds[i];
        reset();
    }
    void prepare(double rate){if(!std::isfinite(rate)||rate<32000||rate>192000)throw std::invalid_argument("Sample rate");rate_=rate;reset();}
    void reset(){voices_={};retiring_={};}
    bool silent()const{for(auto* pool:{&voices_,&retiring_})for(auto& v:*pool)if(v.active)return false;return true;}
    // Change native modal loss, rather than multiplying the complete output tail.
    void envelope(double attack,double decay,double damping){
        attack_=attack;
        if(decay==decayScale_&&damping==damping_)return;
        for(auto* pool:{&voices_,&retiring_})for(auto& v:*pool)if(v.active)for(auto& o:v.modes){
            double extra=o.loss*(1/decay-1/decayScale_)+(damping-damping_)*12*std::sqrt(o.frequency/1000.);
            double ratio=std::exp(-extra/rate_);o.c*=ratio;o.s*=ratio;
        }
        decayScale_=decay;damping_=damping;
    }
    void release(){for(auto* pool:{&voices_,&retiring_})for(auto& v:*pool)if(v.active)v.fadeStep=-v.fade/std::max(1.,rate_*.02);}
    void damp(){for(auto& v:voices_)for(auto& o:v.modes){o.re*=.05;o.im*=.05;}}
    bool strike(std::string_view family,int group,int midi,double velocity,int sourceRoot=-1) {
        if(midi<0||midi>127||!std::isfinite(velocity)||velocity<=0||velocity>1)return false;
        const Model* model=nullptr;
        const int selectionKey=sourceRoot>=0?sourceRoot:(group==82?cornerModelKey(midi):midi);
        for(const auto& m:models_)if(m.family==family&&m.group==group&&(!model||std::abs(m.root-selectionKey)<std::abs(model->root-selectionKey)))model=&m;
        if(!model)return false;
        const auto& anchors=model->anchors;double v=velocity*127;
        std::size_t hi=0;while(hi+1<anchors.size()&&v>anchors[hi].velocity)++hi;
        std::size_t lo=hi?hi-1:hi;
        double mix=hi==lo?0:std::clamp((v-anchors[lo].velocity)/(anchors[hi].velocity-anchors[lo].velocity),0.,1.);
        auto* voice=&voices_[0];for(auto& a:voices_)if(!a.active){voice=&a;break;}else if(a.age>voice->age)voice=&a;
        if(voice->active){auto* tail=&retiring_[0];for(auto& v:retiring_)if(!v.active){tail=&v;break;}else if(v.age>tail->age)tail=&v;*tail=*voice;tail->fadeStep=-tail->fade/std::max(1.,rate_*.003);}
        *voice={};voice->active=true;
        voice->rise=attack_>0?0:1;voice->riseStep=1/std::max(1.,attack_*rate_);
        for(int j=0;j<16;++j) {
            const auto&a=anchors[lo].modes[j];const auto&b=anchors[hi].modes[j];if(a.frequency==0||b.frequency==0)continue;
            auto lerp=[&](double x,double y){return x+(y-x)*mix;};
            double freq=lerp(a.frequency,b.frequency)*std::pow(2.,(midi-model->root)/12.);
            if(freq>=rate_*.45)continue;
            double loss=lerp(a.loss,b.loss);
            double radius=std::exp(-(loss/decayScale_+damping_*12*std::sqrt(freq/1000.))/rate_),angle=6.283185307179586*freq/rate_;
            auto&o=voice->modes[j];o.c=radius*std::cos(angle);o.s=radius*std::sin(angle);o.im=1;
            o.loss=loss;o.frequency=freq;
            double scale=v<anchors.front().velocity?v/anchors.front().velocity:(v>anchors.back().velocity?std::min(2.,v/anchors.back().velocity):1.);
            o.left=scale*lerp(a.left,b.left);o.right=scale*lerp(a.right,b.right);
        }
        // Per-group headroom for uncalibrated articulations and tongue drums.
        double gain=fallbackGain_[size_t(group)];
        for(const auto&d:dynamics_)if(d.family==family&&std::find(d.groups.begin(),d.groups.end(),group)!=d.groups.end()){gain=d.gain;break;}
        for(auto&o:voice->modes){o.left*=gain;o.right*=gain;}return true;
    }
    std::array<double,2> tick(){
        std::array<double,2> out{};
        for(auto* pool:{&voices_,&retiring_})for(auto&v:*pool)if(v.active){double energy=0;
            const double level=v.fade*v.rise*v.rise*(3-2*v.rise);
            v.rise=std::min(1.,v.rise+v.riseStep);
            for(auto&o:v.modes){out[0]+=o.re*o.left*level;out[1]+=o.re*o.right*level;double re=o.re*o.c-o.im*o.s;o.im=o.re*o.s+o.im*o.c;o.re=re;energy+=o.re*o.re+o.im*o.im;}
            v.fade=std::max(0.,v.fade+v.fadeStep);
            if(v.fade==0||energy<1e-12||++v.age>unsigned(rate_*30))v.active=false;
        }return out;
    }
};
}

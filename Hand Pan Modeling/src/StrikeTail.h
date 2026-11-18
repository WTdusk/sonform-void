#pragma once
#include "CornerKeyMap.h"
#include "ActiveIndices.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace handpan {
// Empirical delayed envelope: (exp(-slow*t)-exp(-fast*t))^3.
// Its four exponential terms permit independent, coherent contact accumulation.
class StrikeTail {
    static constexpr int modes=16,anchors=5;
    static constexpr std::array<int,anchors> velocities{7,42,63,91,127};
    using Gains=std::array<std::array<double,2>,modes>;
    struct Model {int root=0;double slow=0,fast=0;std::array<double,modes> frequency{};std::array<Gains,anchors> gain{};};
    struct Osc {double c=0,s=0,loss=0;std::array<double,2> re{},im{};};
    struct Note {bool active=false;unsigned age=0;std::array<std::array<Osc,4>,modes> osc{};double rise=1,riseStep=1;};
    std::vector<Model> models_;
    std::array<Note,128> notes_{};
    ActiveIndices<128> active_;
    double rate_=48000;
    double attack_=0,decayScale_=1,damping_=0;
    const Model& model(int key) const {const int selectionKey=cornerModelKey(key);return *std::min_element(models_.begin(),models_.end(),[selectionKey](const Model&a,const Model&b){return std::abs(a.root-selectionKey)<std::abs(b.root-selectionKey);});}
public:
    void load(std::istream& input){
        std::string line;std::getline(input,line);if(!line.empty()&&line.back()=='\r')line.pop_back();
        if(line!="root,velocity,mode,frequency,slow,fast,left,right")throw std::runtime_error("Strike tail header");
        std::vector<Model> parsed;std::vector<std::array<int,3>> seen;
        while(std::getline(input,line)){
            if(!line.empty()&&line.back()=='\r')line.pop_back();if(line.empty())continue;
            std::istringstream row(line);std::string token;std::array<double,8> v{};
            for(auto& x:v){if(!std::getline(row,token,','))throw std::runtime_error("Strike tail column");size_t used=0;x=std::stod(token,&used);if(used!=token.size()||!std::isfinite(x))throw std::runtime_error("Strike tail number");}
            if(std::getline(row,token,','))throw std::runtime_error("Strike tail extra column");
            for(int i:{0,1,2})if(v[i]!=std::floor(v[i]))throw std::runtime_error("Strike tail index");
            int root=int(v[0]),velocity=int(v[1]),mode=int(v[2]);auto at=std::find(velocities.begin(),velocities.end(),velocity);
            if(root<0||root>127||at==velocities.end()||mode<0||mode>=modes||v[3]<30||v[3]>6000||v[4]<.1||v[4]>2||v[5]<=v[4]||v[5]>16||v[6]<0||v[6]>.2||v[7]<0||v[7]>.2)throw std::runtime_error("Strike tail range");
            std::array<int,3> identity{root,velocity,mode};if(std::find(seen.begin(),seen.end(),identity)!=seen.end())throw std::runtime_error("Strike tail duplicate");seen.push_back(identity);
            auto m=std::find_if(parsed.begin(),parsed.end(),[root](const Model&m){return m.root==root;});
            if(m==parsed.end()){parsed.push_back({});m=parsed.end()-1;m->root=root;m->slow=v[4];m->fast=v[5];}
            if(m->slow!=v[4]||m->fast!=v[5]||(m->frequency[mode]!=0&&m->frequency[mode]!=v[3]))throw std::runtime_error("Strike tail topology");
            m->frequency[mode]=v[3];m->gain[size_t(at-velocities.begin())][mode]={v[6],v[7]};
        }
        if(parsed.size()!=8||seen.size()!=8*anchors*modes)throw std::runtime_error("Incomplete Strike tail response");
        std::sort(parsed.begin(),parsed.end(),[](const Model&a,const Model&b){return a.root<b.root;});
        const std::array<int,8> keys{21,24,26,28,29,31,33,36};for(size_t i=0;i<keys.size();++i)if(parsed[i].root!=keys[i])throw std::runtime_error("Strike tail keys");
        models_=std::move(parsed);reset();
    }
    void prepare(double rate){
        if(models_.empty()||!std::isfinite(rate)||rate<32000||rate>192000)throw std::runtime_error("Strike tail prepare");rate_=rate;
        for(int key=0;key<128;++key){const auto& m=model(key);
            for(int j=0;j<modes;++j){double f=m.frequency[j]*std::pow(2.,(key-m.root)/12.);double angle=6.283185307179586*f/rate;
                for(int b=0;b<4;++b){auto& o=notes_[key].osc[j][b];o.loss=(3-b)*m.slow+b*m.fast;double radius=f<rate*.45?std::exp(-(o.loss/decayScale_+damping_*12)/rate):0;o.c=radius*std::cos(angle);o.s=radius*std::sin(angle);}
            }
        }reset();
    }
    bool silent()const{return active_.empty();}
    void reset(){active_.clear();for(auto& n:notes_){n.active=false;n.age=0;for(auto& mode:n.osc)for(auto& o:mode){o.re={};o.im={};}}}
    void damp(){for(auto& n:notes_)for(auto& mode:n.osc)for(auto& o:mode)for(int ch=0;ch<2;++ch){o.re[ch]*=.05;o.im[ch]*=.05;}}
    void envelope(double attack,double decay,double damping){
        attack_=attack;if(decay==decayScale_&&damping==damping_)return;
        for(auto& n:notes_)for(auto& mode:n.osc)for(auto& o:mode){double ratio=std::exp(-(o.loss*(1/decay-1/decayScale_)+(damping-damping_)*12)/rate_);o.c*=ratio;o.s*=ratio;}
        decayScale_=decay;damping_=damping;
    }
    void strike(int key,double velocity){
        if(key<0||key>127||!std::isfinite(velocity)||velocity<=0||velocity>1)return;
        const auto& m=model(key);auto& n=notes_[key];double v=velocity*127;int hi=0;while(hi<anchors-1&&v>velocities[hi])++hi;int lo=hi?hi-1:hi;
        double mix=hi==lo?0:std::clamp((v-velocities[lo])/double(velocities[hi]-velocities[lo]),0.,1.);double scale=v<velocities[0]?v/velocities[0]:1;
        const std::array<double,4> weights{1,-3,3,-1};
        for(int j=0;j<modes;++j)for(int b=0;b<4;++b)for(int ch=0;ch<2;++ch)n.osc[j][b].im[ch]+=weights[b]*scale*(m.gain[lo][j][ch]*(1-mix)+m.gain[hi][j][ch]*mix);
        if(!n.active){active_.add(unsigned(key));n.rise=attack_>0?0:1;n.riseStep=1/std::max(1.,attack_*rate_);}n.active=true;n.age=0;
    }
    std::array<double,2> tick(){
        std::array<double,2> out{};
        for(unsigned i=0;i<active_.size();){auto& n=notes_[active_[i]];double energy=0;
            double level=n.rise*n.rise*(3-2*n.rise);n.rise=std::min(1.,n.rise+n.riseStep);
            for(auto& mode:n.osc)for(auto& o:mode)for(int ch=0;ch<2;++ch){out[ch]+=level*o.re[ch];double re=o.re[ch]*o.c-o.im[ch]*o.s;o.im[ch]=o.re[ch]*o.s+o.im[ch]*o.c;o.re[ch]=re;energy+=re*re+o.im[ch]*o.im[ch];}
            if(energy<1e-18||++n.age>unsigned(rate_*30)){n.active=false;active_.erase(i);}else ++i;
        }return out;
    }
};
}

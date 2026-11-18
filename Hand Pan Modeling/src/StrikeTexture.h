#pragma once
#include "CornerKeyMap.h"
#include "ActiveIndices.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace handpan {
// Empirical stochastic residual, separately fitted from tonal vibration.
// Repeated contacts add envelope power; there is no reference PCM at runtime.
class StrikeTexture {
    static constexpr int bands=8,branches=4,anchors=5;
    using Gains=std::array<std::array<std::array<double,2>,branches>,bands>;
    struct Model {int root=0;std::array<double,anchors> velocity{};std::array<Gains,anchors> gains{};};
    struct Filter {double b=0,a1=0,a2=0;bool enabled=false;std::array<double,2> x1{},x2{},y1{},y2{};};
    struct Note {bool active=false;std::uint64_t age=0;std::uint32_t random=1;Gains power{};std::array<Filter,bands> filters{};double rise=1,riseStep=1;};
    std::vector<Model> models_;
    std::array<double,bands> frequency_{},q_{};
    std::array<double,branches> loss_{},decay_{};
    std::array<Note,128> notes_{};
    ActiveIndices<128> active_;
    double rate_=48000;
    double attack_=0,lastDecay_=-1,lastDamping_=-1;
    const Model& model(int key) const {
        const int selectionKey=cornerModelKey(key);
        return *std::min_element(models_.begin(),models_.end(),[selectionKey](const Model&a,const Model&b){return std::abs(a.root-selectionKey)<std::abs(b.root-selectionKey);});
    }
    static double white(std::uint32_t& r){r^=r<<13;r^=r>>17;r^=r<<5;return (double(r)/4294967295.*2-1)*1.7320508075688772;}
public:
    void load(std::istream& input){
        StrikeTexture parsed;std::string line;
        std::getline(input,line);if(!line.empty()&&line.back()=='\r')line.pop_back();
        if(line!="root,velocity,band,frequency,q,branch,loss,left,right")throw std::runtime_error("Strike texture header");
        struct Seen {int root,velocity,band,branch;};std::vector<Seen> seen;
        std::array<bool,bands> bandSeen{};std::array<bool,branches> branchSeen{};
        while(std::getline(input,line)){
            if(!line.empty()&&line.back()=='\r')line.pop_back();if(line.empty())continue;
            std::istringstream row(line);std::string token;std::array<double,9> v{};
            for(auto& value:v){if(!std::getline(row,token,','))throw std::runtime_error("Strike texture column");size_t used=0;value=std::stod(token,&used);if(used!=token.size()||!std::isfinite(value))throw std::runtime_error("Strike texture number");}
            if(std::getline(row,token,','))throw std::runtime_error("Strike texture extra column");
            for(int i:{0,1,2,5})if(v[i]!=std::floor(v[i]))throw std::runtime_error("Strike texture index");
            int root=int(v[0]),velocity=int(v[1]),band=int(v[2]),branch=int(v[5]);
            const std::array<int,5> velocities{7,42,63,91,127};auto at=std::find(velocities.begin(),velocities.end(),velocity);
            if(root<0||root>127||at==velocities.end()||band<0||band>=bands||branch<0||branch>=branches||v[3]<30||v[3]>22000||v[4]<.5||v[4]>20||v[6]<.1||v[6]>1500||v[7]<0||v[7]>.25||v[8]<0||v[8]>.25)throw std::runtime_error("Strike texture range");
            for(auto s:seen)if(s.root==root&&s.velocity==velocity&&s.band==band&&s.branch==branch)throw std::runtime_error("Strike texture duplicate");
            seen.push_back({root,velocity,band,branch});
            if(bandSeen[band]&&(parsed.frequency_[band]!=v[3]||parsed.q_[band]!=v[4]))throw std::runtime_error("Strike texture band topology");
            if(branchSeen[branch]&&parsed.loss_[branch]!=v[6])throw std::runtime_error("Strike texture decay topology");
            bandSeen[band]=true;branchSeen[branch]=true;parsed.frequency_[band]=v[3];parsed.q_[band]=v[4];parsed.loss_[branch]=v[6];
            auto m=std::find_if(parsed.models_.begin(),parsed.models_.end(),[root](const Model&m){return m.root==root;});
            if(m==parsed.models_.end()){parsed.models_.push_back({});m=parsed.models_.end()-1;m->root=root;for(int i=0;i<anchors;++i)m->velocity[i]=velocities[i];}
            m->gains[size_t(at-velocities.begin())][band][branch]={v[7],v[8]};
        }
        if(parsed.models_.size()!=8||seen.size()!=8*anchors*bands*branches)throw std::runtime_error("Incomplete Strike texture");
        std::sort(parsed.models_.begin(),parsed.models_.end(),[](const Model&a,const Model&b){return a.root<b.root;});
        const std::array<int,8> keys{21,24,26,28,29,31,33,36};for(size_t i=0;i<keys.size();++i)if(parsed.models_[i].root!=keys[i])throw std::runtime_error("Strike texture keys");
        *this=std::move(parsed);
    }
    void prepare(double rate){
        if(models_.empty()||!std::isfinite(rate)||rate<32000||rate>192000)throw std::runtime_error("Strike texture prepare");
        rate_=rate;lastDecay_=lastDamping_=-1;for(int j=0;j<branches;++j)decay_[j]=std::exp(-2*loss_[j]/rate);
        for(int key=0;key<128;++key)for(int b=0;b<bands;++b){
            auto& f=notes_[key].filters[b];double frequency=frequency_[b]*std::pow(2.,(key-model(key).root)/12.);
            f.enabled=frequency<rate*.45;
            if(f.enabled){double w=6.283185307179586*frequency/rate;double alpha=std::sin(w)/(2*q_[b]);f.b=alpha/(1+alpha);f.a1=-2*std::cos(w)/(1+alpha);f.a2=(1-alpha)/(1+alpha);}
        }reset();
    }
    bool silent()const{return active_.empty();}
    void reset(){active_.clear();for(size_t key=0;key<notes_.size();++key){auto& n=notes_[key];n.active=false;n.age=0;n.power={};n.random=0x915ab137u+std::uint32_t(key)*0x9e3779b9u;if(!n.random)n.random=1;for(auto& f:n.filters){f.x1={};f.x2={};f.y1={};f.y2={};}}}
    void damp(){for(auto& n:notes_)for(auto& b:n.power)for(auto& p:b)for(auto& v:p)v*=.0025;}
    void envelope(double attack,double decay,double damping){attack_=attack;if(decay==lastDecay_&&damping==lastDamping_)return;lastDecay_=decay;lastDamping_=damping;for(int j=0;j<branches;++j)decay_[j]=std::exp(-2*(loss_[j]/decay+damping*12)/rate_);}
    void strike(int key,double velocity){
        if(key<0||key>127||!std::isfinite(velocity)||velocity<=0||velocity>1)return;
        auto& n=notes_[key];const auto& m=model(key);double v=velocity*127;int hi=0;while(hi<anchors-1&&v>m.velocity[hi])++hi;
        int lo=hi?hi-1:hi;double mix=hi==lo?0:std::clamp((v-m.velocity[lo])/(m.velocity[hi]-m.velocity[lo]),0.,1.);double scale=v<m.velocity[0]?v/m.velocity[0]:1;
        for(int b=0;b<bands;++b)for(int j=0;j<branches;++j)for(int ch=0;ch<2;++ch){double amp=(m.gains[lo][b][j][ch]*(1-mix)+m.gains[hi][b][j][ch]*mix)*scale;n.power[b][j][ch]+=amp*amp;}
        if(!n.active){active_.add(unsigned(key));n.rise=attack_>0?0:1;n.riseStep=1/std::max(1.,attack_*rate_);}n.active=true;n.age=0;
    }
    std::array<double,2> tick(){
        std::array<double,2> out{};
        for(unsigned i=0;i<active_.size();){auto& n=notes_[active_[i]];double total=0;
            double level=n.rise*n.rise*(3-2*n.rise);n.rise=std::min(1.,n.rise+n.riseStep);
            for(int b=0;b<bands;++b){auto& f=n.filters[b];
                for(int ch=0;ch<2;++ch){double power=0;for(int j=0;j<branches;++j){power+=n.power[b][j][ch];n.power[b][j][ch]*=decay_[j];}total+=power;
                    if(!f.enabled)continue;
                    double x=white(n.random);double y=f.b*(x-f.x2[ch])-f.a1*f.y1[ch]-f.a2*f.y2[ch];f.x2[ch]=f.x1[ch];f.x1[ch]=x;f.y2[ch]=f.y1[ch];f.y1[ch]=y;
                    out[ch]+=level*y*std::sqrt(std::max(0.,power)/f.b);
                }
            }
            if(total<1e-16||++n.age>std::uint64_t(rate_*30)){n.active=false;active_.erase(i);}else ++i;
        }return out;
    }
};
}

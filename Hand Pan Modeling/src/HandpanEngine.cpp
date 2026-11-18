#include "HandpanEngine.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace handpan {
namespace {
constexpr double pi=3.14159265358979323846;
constexpr std::array<double,12> ratio{1,2,3,1,2,4.73,6.17,8.41,3,6,6,9};
constexpr std::array<double,12> decay{5.8,4.1,2.9,4.9,3.7,0.85,0.48,0.24,3.6,2.6,2.3,1.5};
constexpr std::array<double,12> radiation{1,.53,.33,.19,.13,.13,.085,.055,.19,.11,.075,.045};
}
void Engine::coefficients(Mode& m,double extra) {
    if(m.w<=0){m.a=m.d=1;m.b=m.c=0;return;}
    const double s=m.sigma/decayScale_+extra+damping_*12*std::sqrt(m.w/(2*pi*1000));
    const double wd=std::sqrt(m.w*m.w-s*s);
    const double sn=std::sin(wd*h_)/wd, c=std::cos(wd*h_), e=std::exp(-s*h_);
    m.a=e*(c+s*sn); m.b=e*sn;
    m.c=-e*m.w*m.w*sn; m.d=e*(c-s*sn);
}
void Engine::prepare(double sr,Settings settings) {
    if(!std::isfinite(sr)||sr<32000||sr>192000)
        throw std::invalid_argument("Sample rate must be 32000..192000 Hz");
    if(settings.fieldCount<1||settings.fieldCount>fields)throw std::invalid_argument("Physical field count");
    for(int f=0;f<settings.fieldCount;++f)if(settings.tuning[size_t(f)]<0||settings.tuning[size_t(f)]>127)throw std::invalid_argument("Physical field tuning");
    if(!std::isfinite(settings.air)||settings.air<0||settings.air>1
       ||!std::isfinite(settings.air)||settings.air<0||settings.air>1
       ||!std::isfinite(settings.air)||settings.air<0||settings.air>1||settings.oversampling<2||settings.oversampling>8
       ||!std::isfinite(settings.coupling)||settings.coupling<0||settings.coupling>2
       ||!std::isfinite(settings.nonlinearity)||settings.nonlinearity<0||settings.nonlinearity>2
       ||!std::isfinite(settings.contactNoise)||settings.contactNoise<0||settings.contactNoise>0.1)
        throw std::invalid_argument("Settings outside research preset range");
    rate_=sr; settings_=settings; h_=1/(sr*settings.oversampling); edgeCount_=0;
    firLength_=32*settings.oversampling+1;
    const double cutoff=.45/settings.oversampling;
    double filterSum=0;
    for(int i=0;i<firLength_;++i) {
        const int n=i-firLength_/2;
        const double sinc=n==0?2*cutoff:std::sin(2*pi*cutoff*n)/(pi*n);
        fir_[i]=sinc*(.42-.5*std::cos(2*pi*i/(firLength_-1))+.08*std::cos(4*pi*i/(firLength_-1)));
        filterSum+=fir_[i];
    }
    for(int i=0;i<firLength_;++i) fir_[i]/=filterSum;
    modes_={};
    for(int f=0;f<settings.fieldCount;++f) {
        const double fundamental=440*std::pow(2.0,(settings.tuning[size_t(f)]-69)/12.0);
        const double pan=f==0?0:0.36*std::sin(2*pi*(f-1)/8.0);
        for(int j=0;j<modesPerField;++j) {
            auto& m=modes_[f*modesPerField+j];
            m={};
            double freq=fundamental*ratio[j];
            if(j==1) freq*=1.0005;
            if(j==2) freq*=0.9992;
            if(j==3) freq+=0.72+f*0.043;
            if(j==4) freq+=1.15+f*0.057;
            if(j==8) freq=fundamental*3*.9992+1.6+f*.09;
            if(j==10) freq+=2.3+f*.11;
            m.w=2*pi*freq;
            const double sustain=j>=5&&j<modesPerField ? 1+1.5*settings.air : 1;
            m.sigma=std::log(1000.0)/(decay[j]*sustain*std::pow(146.832/fundamental,.22));
            const double spread=j>=5 ? .035+.14*settings.air : .035;
            const double modalPan=std::clamp(pan+(j%2?spread:-spread),-.8,.8);
            const double gain=j>=8?settings.air:(j>=5?1+.5*settings.air:1);
            m.outL=gain*radiation[j]*std::sqrt((1-modalPan)/2.0);
            m.outR=gain*radiation[j]*std::sqrt((1+modalPan)/2.0);
            coefficients(m,0);
        }
    }
    const std::array<double,4> sharedFreq{87,337,719,1193};
    const std::array<double,4> sharedDecay{.75,.62,.39,.25};
    for(int j=0;j<4;++j) {
        auto& m=modes_[fieldModes+j]; m={};
        m.w=2*pi*sharedFreq[j]; m.sigma=std::log(1000.0)/sharedDecay[j];
        m.outL=(j==0?.5:.13)*.7071; m.outR=m.outL;
        coefficients(m,0);
    }
    // Positive coupling potentials: k/2 * (q_high - shape*q_low^2)^2.
    // Linear spring edges use shape=-1.
    for(int f=0;f<settings.fieldCount;++f) {
        int base=f*modesPerField;
        const double w=modes_[base].w;
        addEdge(base,base+1,w*w*.025*settings.nonlinearity,1.6);
        addEdge(base,base+2,w*w*.012*settings.nonlinearity,1.1);
        addEdge(base,fieldModes,w*w*.0010*settings.coupling,-1);
        addEdge(base+5,fieldModes+1,w*w*.0018*settings.coupling,-1);
    }
    // Reciprocal, frequency-selective cross-field springs.
    for(int a=0;a<settings.fieldCount*modesPerField;++a) for(int b=a+1;b<settings.fieldCount*modesPerField;++b) {
        if(a/modesPerField==b/modesPerField || a%modesPerField>4 || b%modesPerField>4) continue;
        const double wa=modes_[a].w, wb=modes_[b].w;
        const double distance=std::abs(std::log(wa/wb));
        if(distance<.018) addEdge(a,b,std::min(wa*wa,wb*wb)*.00065*
                                 std::exp(-distance*distance/.00008)*settings.coupling,-1);
    }
    reset();
}
void Engine::addEdge(int a,int b,double k,double shape) {
    if(k==0) return;
    if(edgeCount_>=int(edges_.size())) throw std::logic_error("Edge capacity");
    edges_[edgeCount_++]={a,b,k,shape};
}
void Engine::reset() {
    for(auto& m:modes_) {m.q=0;m.p=0;coefficients(m,0);}
    for(auto& hit:hits_) hit={};activeHits_.clear();sleeping_=true;silenceClock_=0;
    touch_.fill(0); noiseLow_=0; random_=0x918273ab;
    history_.fill({}); historyPosition_=0;
}
const TechniqueProfile& Engine::profile(Technique technique) const {
    const int t=int(technique);
    if(t<0||t>=techniqueCount)throw std::invalid_argument("Technique index");
    return profiles_[t];
}
void Engine::setProfile(Technique technique,const TechniqueProfile& p) {
    profile(technique);
    if(!std::isfinite(p.contactLoss)||p.contactLoss<0||p.contactLoss>200
       ||!std::isfinite(p.holdSeconds)||p.holdSeconds<0||p.holdSeconds>.05)
        throw std::invalid_argument("Contact loss/hold range");
    for(int i=0;i<5;++i){const auto& a=p.points[i];
        if(!std::isfinite(a.velocity)||a.velocity<0||a.velocity>1||(i&&a.velocity<=p.points[i-1].velocity)
           ||!std::isfinite(a.impulse)||a.impulse<0||a.impulse>1
           ||!std::isfinite(a.contactSeconds)||a.contactSeconds<.0004||a.contactSeconds>.01)
            throw std::invalid_argument("Dynamic anchor range/order");
        for(double v:{a.noise,a.fundamental,a.octave,a.fifth,a.metal,a.body})
            if(!std::isfinite(v)||v<0||v>10)throw std::invalid_argument("Dynamic spectral weight range");
    }
    if(p.points.front().velocity!=0||p.points.back().velocity!=1||p.points.front().impulse!=0||p.points.front().noise!=0)
        throw std::invalid_argument("Dynamic endpoints");
    profiles_[int(technique)]=p;
}
bool Engine::strike(int field,double velocity,double position,double hardness,Technique technique) {
    if(int(technique)<0||int(technique)>=techniqueCount)return false;
    if(field<0||field>=settings_.fieldCount||!std::isfinite(velocity)||!std::isfinite(position)||!std::isfinite(hardness)) return false;
    velocity=std::clamp(velocity,0.0,1.0);
    if(velocity==0) return true;
    Hit* hit=nullptr;
    for(auto& candidate:hits_) if(!candidate.active){hit=&candidate;break;}
    if(!hit) return false; // Existing mechanical state is never stolen.
    position=std::clamp(position,0.0,1.0); hardness=std::clamp(hardness,0.0,1.0);
    *hit={}; hit->active=true;sleeping_=false;activeHits_.add(unsigned(hit-hits_.data()));
    const auto& p=profiles_[int(technique)];
    const auto dynamic=interpolate(p,velocity);
    const double naturalDuration=std::max(.00025,dynamic.contactSeconds-.0010*hardness);
    const double duration=std::max(naturalDuration,attack_);
    hit->contactLoss*=naturalDuration/duration;
    hit->length=std::max(8,int(duration/h_));
    hit->field=field;hit->contactLoss=p.contactLoss;
    hit->shortLength=std::max(8,int((.00030+.00015*(1-hardness))/h_));
    hit->lifetime=std::max(hit->length,hit->shortLength)+int(p.holdSeconds/h_);
    hit->amplitude=dynamic.impulse;
    hit->noise=settings_.contactNoise*dynamic.noise*(0.3+hardness)*std::sqrt(naturalDuration/duration);
    for(int j=0;j<modesPerField;++j) {
        // Analytic strike-position prior, to be replaced by measured modal projections.
        const double positionWeight=j<5 ? (j%3==0?1-.45*position:.36+1.15*position)
                                            : (.18+1.8*position)*(0.35+.65*hardness);
        const double weight=j==3?.26:j==4?.2:j>=5?.65:1;
        const double spectrum=(j==0||j==3)?dynamic.fundamental:(j==1||j==4)?dynamic.octave:(j==2||j==8)?dynamic.fifth:dynamic.metal;
        hit->weights[j]=modes_[field*modesPerField+j].w*weight*positionWeight*spectrum;
    }
    hit->weights[modesPerField]=modes_[fieldModes].w*(field==0?.18:.035);
    hit->weights[modesPerField+1]=modes_[fieldModes+1].w*.025*(.2+position);
    hit->weights[modesPerField+2]=modes_[fieldModes+2].w*.017*position;
    hit->weights[modesPerField+3]=modes_[fieldModes+3].w*.012*position;
    for(int j=modesPerField;j<modesPerField+4;++j)hit->weights[j]*=dynamic.body;
    return true;
}
void Engine::touch(int field,double amount) {
    if(field<0||field>=fields||!std::isfinite(amount)) return;
    touch_[field]=std::clamp(amount,0.0,1.0);
    for(int j=0;j<modesPerField;++j) coefficients(modes_[field*modesPerField+j],touch_[field]*(32+9*j));
}
void Engine::envelope(double attack,double decay,double damping){
    attack_=attack;if(decay==decayScale_&&damping==damping_)return;
    decayScale_=decay;damping_=damping;
    for(int i=0;i<totalModes;++i)coefficients(modes_[i],i<fieldModes?touch_[i/modesPerField]*(32+9*(i%modesPerField)):0);
}
void Engine::kick(double dt) {
    for(int i=0;i<edgeCount_;++i) {
        const auto& e=edges_[i]; auto& a=modes_[e.low]; auto& b=modes_[e.high];
        if(e.shape<0) {
            const double impulse=dt*e.k*(b.q-a.q);
            a.p+=impulse; b.p-=impulse;
        } else {
            const double residual=b.q-e.shape*a.q*a.q;
            const double impulse=dt*e.k*residual;
            a.p+=2*e.shape*a.q*impulse; b.p-=impulse;
        }
    }
}
Stereo Engine::tick() {
    Stereo output;if(sleeping_)return output;
    for(int step=0;step<settings_.oversampling;++step) {
        kick(h_*.5);
        double contact=0;
        for(unsigned hitIndex=0;hitIndex<activeHits_.size();) {
            auto& hit=hits_[activeHits_[hitIndex]];
            // Half-sine force with a discrete normalization: integrated impulse is fixed.
            const double phase=pi*(hit.age+.5)/hit.length;
            const double envelope=hit.age<hit.length?std::sin(phase):0;
            const double pulse=envelope*std::sin(pi/(2*hit.length));
            const double fastPulse=hit.age<hit.shortLength ?
                std::sin(pi*(hit.age+.5)/hit.shortLength)*std::sin(pi/(2*hit.shortLength)) : 0;
            for(int local=0;local<modesPerField;++local){
                const double blend=settings_.air*(local>=5?.7:(local==2?.3:0));
                modes_[hit.field*modesPerField+local].p+=hit.amplitude*((1-blend)*pulse+blend*fastPulse)*hit.weights[local];
            }
            for(int j=0;j<4;++j)modes_[fieldModes+j].p+=hit.amplitude*pulse*hit.weights[modesPerField+j];
            random_^=random_<<13; random_^=random_>>17; random_^=random_<<5;
            const double noise=double(random_)/4294967295.0*2-1;
            noiseLow_+=.12*(noise-noiseLow_);
            contact+=hit.noise*envelope*(noise-noiseLow_);
            // Dissipative finger/palm contact acts on the current field state,
            // including older strikes. It never overwrites the modal tail.
            const double contactEnvelope=hit.age<hit.length?envelope:
                double(hit.lifetime-hit.age)/std::max(1,hit.lifetime-hit.length);
            const double loss=std::exp(-hit.contactLoss*contactEnvelope*h_);
            for(int j=0;j<modesPerField;++j)modes_[hit.field*modesPerField+j].p*=loss;
            if(++hit.age>=hit.lifetime){hit.active=false;activeHits_.erase(hitIndex);}else ++hitIndex;
        }
        for(auto& m:modes_) {
            const double q=m.a*m.q+m.b*m.p;
            m.p=m.c*m.q+m.d*m.p; m.q=q;
        }
        kick(h_*.5);
        Stereo instant{contact,contact};
        for(const auto& m:modes_) {instant.left+=m.outL*m.q;instant.right+=m.outR*m.q;}
        history_[historyPosition_]=instant;
        historyPosition_=(historyPosition_+1)%firLength_;
    }
    // Blackman-windowed FIR decimator; group delay is 16 host samples.
    int position=historyPosition_;
    for(int i=0;i<firLength_;++i) {
        if(--position<0) position=firLength_-1;
        output.left+=fir_[i]*history_[position].left;
        output.right+=fir_[i]*history_[position].right;
    }
    // The energy gate is far below audible output and is checked only every
    // 256 host samples. Coupling energy is included; resonant fields stay alive.
    if(++silenceClock_>=256){silenceClock_=0;if(activeHits_.empty()&&energy()<1e-18){sleeping_=true;for(auto& m:modes_){m.q=m.p=0;}history_.fill({});}}
    return output;
}
void Engine::process(float* left,float* right,std::size_t n) {
    for(std::size_t i=0;i<n;++i) {const auto s=tick();left[i]=float(s.left);right[i]=float(s.right);}
}
double Engine::fieldEnergy(int field) const {
    if(field<0||field>=fields) return 0;
    double energy=0;
    for(int j=0;j<modesPerField;++j){const auto& m=modes_[field*modesPerField+j];energy+=.5*(m.p*m.p+m.w*m.w*m.q*m.q);}
    return energy; // Local uncoupled energy; coupling potential is counted in energy().
}
double Engine::energy() const {
    double result=0;
    for(const auto& m:modes_) result+=.5*(m.p*m.p+m.w*m.w*m.q*m.q);
    for(int i=0;i<edgeCount_;++i){const auto&e=edges_[i];
        double d=e.shape<0?modes_[e.high].q-modes_[e.low].q:
                            modes_[e.high].q-e.shape*modes_[e.low].q*modes_[e.low].q;
        result+=.5*e.k*d*d;
    }
    return result;
}
double Engine::frequency(int field,int mode) const {
    if(field<0||field>=fields||mode<0||mode>=modesPerField) return 0;
    return modes_[field*modesPerField+mode].w/(2*pi);
}
}



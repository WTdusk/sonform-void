#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>
#include "Articulation.h"
#include "ActiveIndices.h"

namespace handpan {
struct Stereo { double left = 0, right = 0; };
struct Settings {
    double coupling = 1.0;       // Fixed at prepare; 0 disables structure connections.
    double nonlinearity = 1.0;   // Fixed at prepare; 0 gives linear mechanics.
    double contactNoise = 0.018;
    int oversampling = 4;
    double air = 0.0;            // Warm reference direction; airy experiment remains opt-in.
    int fieldCount = 9;
    std::array<int,23> tuning{50,57,58,60,62,64,65,69,72};
};

// Research preset: modal units, not identified material/geometry parameters.
// One instance represents a complete continuously vibrating instrument.
class Engine {
public:
    static constexpr int fields = 23, modesPerField = 12;
    static constexpr int fieldModes = fields * modesPerField;
    static constexpr int totalModes = fields * modesPerField + 4;
    void prepare(double sampleRate, Settings settings = {});
    void reset();
    // position: 0 = centre, 1 = shoulder; hardness: 0 = soft finger, 1 = hard tip.
    bool strike(int field, double velocity, double position = 0.25, double hardness = 0.4,
                Technique technique = Technique::Fingerpad);
    // Configure before rendering/on a stopped audio thread. Validates all rows atomically.
    void setProfile(Technique technique, const TechniqueProfile& profile);
    const TechniqueProfile& profile(Technique technique) const;
    void touch(int field, double amount);
    void envelope(double attack,double decay,double damping);
    Stereo tick();
    bool silent()const{return sleeping_;}
    void process(float* left, float* right, std::size_t frames);
    double energy() const;
    double fieldEnergy(int field) const;
    double frequency(int field, int mode = 0) const;
private:
    struct Mode {
        double q=0, p=0, w=0, sigma=0, a=0, b=0, c=0, d=0;
        double outL=0, outR=0;
    };
    struct Edge { int low=0, high=0; double k=0, shape=0; };
    struct Hit {
        bool active=false;
        int age=0, length=0, shortLength=0, lifetime=0, field=0;
        double amplitude=0, noise=0, contactLoss=0;
        std::array<double,modesPerField+4> weights{};
    };
    std::array<Mode,totalModes> modes_{};
    // Fixed capacities allocated at construction, never resized by audio methods.
    std::vector<Edge> edges_=std::vector<Edge>(2048);
    std::vector<Hit> hits_=std::vector<Hit>(32);
    std::array<double,fields> touch_{};
    ActiveIndices<32> activeHits_;
    bool sleeping_=true;unsigned silenceClock_=0;
    int edgeCount_=0;
    double rate_=48000, h_=1.0/192000, noiseLow_=0;
    double attack_=0,decayScale_=1,damping_=0;
    std::uint32_t random_=0x918273ab;
    Settings settings_{};
    std::array<TechniqueProfile,techniqueCount> profiles_=warmProfiles();
    std::array<double,257> fir_{};
    std::array<Stereo,257> history_{};
    int firLength_=129, historyPosition_=0;
    void addEdge(int low,int high,double k,double shape=0);
    void kick(double dt);
    void coefficients(Mode& mode, double extraDamping);
};
}


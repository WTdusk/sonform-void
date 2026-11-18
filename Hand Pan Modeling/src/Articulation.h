#pragma once
#include <array>
#include <cmath>
#include <algorithm>

namespace handpan {
enum class Technique { Fingerpad, FingertipToSlap, Thumb, Knuckle, Muted, SoftMallet, Count };
inline constexpr int techniqueCount=int(Technique::Count);
inline constexpr std::array<const char*,techniqueCount> techniqueNames{
    "finger_whacks","fingertip_to_slap","thumb_whacks","knuckle","muted","mallet"};
struct DynamicPoint {
    double velocity=0, impulse=0, contactSeconds=.002, noise=0;
    double fundamental=1, octave=1, fifth=1, metal=1, body=1;
};
struct TechniqueProfile {
    std::array<DynamicPoint,5> points{};
    double contactLoss=0, holdSeconds=0;
};
inline std::array<TechniqueProfile,techniqueCount> warmProfiles() {
    std::array<TechniqueProfile,techniqueCount> profiles{};
    // Default dynamics parameters for each playing technique.
    constexpr double strength[]{1,.91,1.12,.82,.88,1.05};
    constexpr double duration[]{1,.72,1.3,.6,.9,1.65};
    constexpr double noise[]{1,1.15,.62,1.4,3.8,.16};
    constexpr double low[]{1,.86,1.25,.72,.45,1.08};
    constexpr double oct[]{1,1.22,.87,1.28,.72,1.04};
    constexpr double fifth[]{1,1.35,.72,1.5,.8,.88};
    constexpr double metal[]{1,1.4,.55,1.95,2.6,.38};
    constexpr double body[]{1,.8,1.25,1.3,6,.42};
    for(int t=0;t<techniqueCount;++t)for(int i=0;i<5;++i){
        const double v=i*.25;
        profiles[t].points[i]={v,.48*std::pow(v,1.35)*strength[t],
            (.0024-.0009*v)*duration[t],std::pow(v,1.7)*noise[t],
            low[t],oct[t]*(.76+.24*v),fifth[t]*(.55+.45*v),
            metal[t]*(.28+.72*v),body[t]};
    }
    profiles[int(Technique::Thumb)].contactLoss=12;
    profiles[int(Technique::Muted)].contactLoss=95;
    profiles[int(Technique::Muted)].holdSeconds=.018;
    profiles[int(Technique::SoftMallet)].contactLoss=8;
    return profiles;
}
inline DynamicPoint interpolate(const TechniqueProfile& profile,double velocity) {
    velocity=std::clamp(velocity,0.,1.);
    int high=1;while(high<4&&velocity>profile.points[high].velocity)++high;
    const auto& a=profile.points[high-1];const auto& b=profile.points[high];
    const double w=(velocity-a.velocity)/(b.velocity-a.velocity);
    auto blend=[&](double x,double y){return x+(y-x)*w;};
    return {velocity,blend(a.impulse,b.impulse),blend(a.contactSeconds,b.contactSeconds),blend(a.noise,b.noise),
        blend(a.fundamental,b.fundamental),blend(a.octave,b.octave),blend(a.fifth,b.fifth),blend(a.metal,b.metal),blend(a.body,b.body)};
}
}


#pragma once
#include <array>
#include <algorithm>
#include <cmath>
#include <atomic>

// Stereo-linked peak compression. One detector/gain keeps the stereo image
// stable; makeup and parallel mix are smoothed by the surrounding effects rack.
class StereoCompressor {
    double rate=48000,detector=0,threshold=.1258925,exponent=-2./3.,attack=0,release=0,lastGain=1;
    std::atomic<float> reduction{0};
    double lastDb=1e9,lastRatio=1e9,lastAttack=1e9,lastRelease=1e9;
public:
    void prepare(double sampleRate){rate=sampleRate;lastAttack=lastRelease=1e9;reset();}
    void reset(){detector=0;lastGain=1;reduction.store(0,std::memory_order_relaxed);}
    void configure(double db,double ratio,double attackMs,double releaseMs,bool enabled){
        if(db!=lastDb){threshold=std::pow(10.,db/20);lastDb=db;}if(ratio!=lastRatio){exponent=1/ratio-1;lastRatio=ratio;}
        if(attackMs!=lastAttack){attack=std::exp(-1/(rate*attackMs*.001));lastAttack=attackMs;}if(releaseMs!=lastRelease){release=std::exp(-1/(rate*releaseMs*.001));lastRelease=releaseMs;}
        reduction.store(enabled?float(-20*std::log10(std::max(1e-9,lastGain))):0,std::memory_order_relaxed);
    }
    std::array<double,2> tick(std::array<double,2> input){
        double peak=std::max(std::abs(input[0]),std::abs(input[1])),coefficient=peak>detector?attack:release;
        detector=peak+coefficient*(detector-peak);lastGain=detector>threshold?std::pow(detector/threshold,exponent):1;
        for(auto& sample:input)sample*=lastGain;return input;
    }
    float gainReduction()const{return reduction.load(std::memory_order_relaxed);}
};

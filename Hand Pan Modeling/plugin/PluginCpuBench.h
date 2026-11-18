#pragma once
#include <chrono>
#include <array>
#include <algorithm>

struct PluginCpuRun {double milliseconds=0,peak=0;std::vector<float> audio;};
inline std::unique_ptr<juce::AudioPluginInstance> loadCpuPlugin(juce::VST3PluginFormat& format,const juce::String& path){
    juce::OwnedArray<juce::PluginDescription> descriptions;format.findAllTypesForFile(descriptions,juce::File(path).getFullPathName());if(descriptions.size()!=1)throw std::runtime_error("CPU benchmark: expected one instrument");juce::String error;auto p=format.createInstanceFromDescription(*descriptions[0],48000,256,error);if(!p)throw std::runtime_error(error.toStdString());return p;
}
inline PluginCpuRun cpuRun(juce::AudioPluginInstance& p,int model,int scenario,double rate,int block){
    auto find=[&](const char* name){for(auto* param:p.getParameters())if(param->getName(128)==name)return param;throw std::runtime_error(std::string("Missing CPU benchmark parameter: ")+name);};
    for(auto* param:p.getParameters())param->setValueNotifyingHost(param->getDefaultValue());
    find("Model")->setValueNotifyingHost(float(model)/(modelCount-1));find("Articulation")->setValueNotifyingHost(scenario==4?1.f/21:0);
    if(scenario==3)for(const char* name:{"Gain EQ","Filter EQ","Compress","Granular","Delay","Reverb"})find(name)->setValueNotifyingHost(1);
    p.releaseResources();p.setPlayConfigDetails(0,2,rate,block);p.setNonRealtime(true);p.prepareToPlay(rate,block);
    const int frames=int(rate*2);PluginCpuRun result;result.audio.resize(size_t(frames)*2);juce::AudioBuffer<float> audio(2,block);
    // Exclude the host's first parameter flush from steady-state CPU timing.
    for(int warmup=0;warmup<3;++warmup){juce::MidiBuffer midi;p.processBlock(audio,midi);}
    static constexpr std::array<int,7> low{53,50,47,48,50,45,50};int bottom=scenario==4?57:low[size_t(model)];
    const bool dense=scenario>=2;const int stride=int(rate*.08);juce::ScopedNoDenormals noDenormals;
    for(int start=0;start<frames;start+=block){juce::MidiBuffer midi;
        if(scenario==1&&start<=64&&64<start+block)midi.addEvent(juce::MidiMessage::noteOn(1,bottom+7,juce::uint8(100)),64-start);
        if(dense)for(int t=64;t<frames;t+=stride)if(t>=start&&t<start+block)for(int interval:{0,3,5,7,10,12})midi.addEvent(juce::MidiMessage::noteOn(1,bottom+interval,juce::uint8(80+(t/stride)%40)),t-start);
        auto begin=std::chrono::steady_clock::now();p.processBlock(audio,midi);auto end=std::chrono::steady_clock::now();result.milliseconds+=std::chrono::duration<double,std::milli>(end-begin).count();
        for(int c=0;c<2;++c)for(int n=0;n<block&&start+n<frames;++n){float sample=audio.getSample(c,n);if(!std::isfinite(sample)||std::abs(sample)>1)throw std::runtime_error("CPU benchmark produced invalid audio");result.audio[size_t(c*frames+start+n)]=sample;result.peak=std::max(result.peak,double(std::abs(sample)));}
    }return result;
}
inline int runPluginCpuBench(const juce::String& oldPath,const juce::String& newPath,const juce::String& output){
    juce::Thread::setCurrentThreadAffinityMask(1);
    const bool probe=juce::SystemStats::getEnvironmentVariable("SONFORM_CPU_PROBE","")=="1";
    juce::VST3PluginFormat format;auto baseline=loadCpuPlugin(format,oldPath);std::unique_ptr<juce::AudioPluginInstance> candidate;if(oldPath!=newPath)candidate=loadCpuPlugin(format,newPath);juce::Array<juce::var> rows;
    const std::array<const char*,5> labels{"idle","single","dense","dense_all_fx","corner_dense"};
    for(auto setup:std::array<std::pair<double,int>,2>{{{48000,256},{96000,128}}})for(int model=0;model<modelCount;++model)for(int scenario=0;scenario<5;++scenario){if(scenario==4&&model!=4)continue;if(probe&&(setup.first!=48000||!((model==0&&scenario==2)||(model==4&&scenario==4)||(model==6&&scenario==1))))continue;
        std::array<double,3> oldTimes{},newTimes{};double error=0,peak=0;
        for(int repeat=0;repeat<3;++repeat){PluginCpuRun oldRun,newRun;if(candidate&&repeat%2){newRun=cpuRun(*candidate,model,scenario,setup.first,setup.second);oldRun=cpuRun(*baseline,model,scenario,setup.first,setup.second);}else{oldRun=cpuRun(*baseline,model,scenario,setup.first,setup.second);if(candidate)newRun=cpuRun(*candidate,model,scenario,setup.first,setup.second);}
            oldTimes[size_t(repeat)]=oldRun.milliseconds;peak=std::max(peak,oldRun.peak);if(candidate){newTimes[size_t(repeat)]=newRun.milliseconds;for(size_t i=0;i<oldRun.audio.size();++i)error=std::max(error,std::abs(double(oldRun.audio[i])-newRun.audio[i]));}
        }
        std::sort(oldTimes.begin(),oldTimes.end());std::sort(newTimes.begin(),newTimes.end());auto* row=new juce::DynamicObject;row->setProperty("model",modelRegistry()[size_t(model)].id);row->setProperty("scenario",labels[size_t(scenario)]);row->setProperty("sample_rate",setup.first);row->setProperty("block_size",setup.second);row->setProperty("audio_seconds",2);row->setProperty("baseline_ms",oldTimes[1]);row->setProperty("baseline_peak",peak);
        if(candidate){row->setProperty("optimised_ms",newTimes[1]);row->setProperty("reduction_percent",100*(1-newTimes[1]/oldTimes[1]));row->setProperty("max_sample_error",error);}rows.add(juce::var(row));
        std::cout<<int(setup.first)<<" "<<modelRegistry()[size_t(model)].id<<" "<<labels[size_t(scenario)]<<" old="<<oldTimes[1];if(candidate)std::cout<<" new="<<newTimes[1]<<" error="<<error;std::cout<<std::endl;
    }
    auto* report=new juce::DynamicObject;report->setProperty("scope","actual VST3 processBlock; includes instrument and FX, excludes UI and host event construction");report->setProperty("method","median of three interleaved runs, 2 seconds each; fresh prepare per run; first three tests use default bypassed FX");report->setProperty("baseline_path",oldPath);report->setProperty("candidate_path",newPath);report->setProperty("cpu",juce::SystemStats::getCpuModel());report->setProperty("measurements",juce::var(rows));if(!juce::File(output).replaceWithText(juce::JSON::toString(juce::var(report))))throw std::runtime_error("Cannot save plugin CPU report");return 0;
}

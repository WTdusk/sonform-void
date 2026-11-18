#include <juce_audio_utils/juce_audio_utils.h>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>
#include "ModelRegistry.h"
#include "PluginCpuBench.h"

int main(int argc, char** argv) { try {
    if(argc==5&&juce::String(argv[1])=="--cpu-compare"){juce::ScopedJuceInitialiser_GUI init;return runPluginCpuBench(argv[2],argv[3],argv[4]);}
    if (argc != 4) throw std::runtime_error("Usage: PluginRender plugin.vst3 jobs.tsv output_directory");
    juce::ScopedJuceInitialiser_GUI init;
    juce::VST3PluginFormat format;
    juce::OwnedArray<juce::PluginDescription> types;
    format.findAllTypesForFile(types, juce::File(argv[1]).getFullPathName());
    if (types.size() != 1) throw std::runtime_error("Expected one VST3 instrument");
    juce::String error;
    auto plugin = format.createInstanceFromDescription(*types[0], 48000, 256, error);
    if (!plugin) throw std::runtime_error(error.toStdString());
    auto parameters = plugin->getParameters();
    auto parameter = [&](const juce::String& name) -> juce::AudioProcessorParameter* {
        // VST3 exposes wrapper parameter IDs; identify the public parameter names.
        for (auto* p : parameters) if (p->getName(128) == name) return p;
        throw std::runtime_error("Missing parameter: " + name.toStdString());
    };
    auto* model = parameter("Model"); auto* art = parameter("Articulation"); auto* gain = parameter("Output");
    auto directory = juce::File(argv[3]); directory.createDirectory();
    std::ifstream jobs(argv[2]); if (!jobs) throw std::runtime_error("Cannot read jobs");
    std::string line; std::getline(jobs, line); int count = 0;
    while (std::getline(jobs, line)) {
        if (line.empty()) continue;
        std::istringstream row(line); std::string id, family, token;
        std::getline(row, id, '\t'); std::getline(row, family, '\t');
        std::getline(row, token, '\t'); int group = std::stoi(token);
        std::getline(row, token, '\t'); int key = std::stoi(token);
        std::getline(row, token, '\t'); int velocity = std::stoi(token);
        std::getline(row, token, '\t'); int repeats = std::stoi(token);
        std::getline(row, token, '\t'); int interval = std::stoi(token);
        std::getline(row, token, '\t'); int frames = std::stoi(token);
        int mi = -1, ai = -1;
        mi = modelIndexForID(family);
        if (mi >= 0) {
            auto& list = modelRegistry()[size_t(mi)].articulations;
            for (size_t j = 0; j < list.size(); ++j) if (list[j].group == group) ai = int(j);
        }
        if (mi < 0 || ai < 0 || frames < 1 || velocity < 1 || velocity > 127 || interval < 1 || repeats < 0)
            throw std::runtime_error("Invalid job " + id);
        struct Event {int frame,key,velocity;};
        std::vector<Event> events;
        // repeats=0 reads an explicit performance alongside the job manifest.
        // Ordinary diagnostic jobs keep their original scheduling unchanged.
        if(repeats==0){
            auto path=juce::File(argv[2]).getParentDirectory().getChildFile(id+".events.tsv");
            std::ifstream input(path.getFullPathName().toStdString());
            if(!input)throw std::runtime_error("Cannot read performance "+id);
            std::string eventLine;std::getline(input,eventLine);
            while(std::getline(input,eventLine)){
                std::istringstream e(eventLine);Event event{};
                if(!(e>>event.frame>>event.key>>event.velocity)||event.frame<0||event.frame>=frames||event.key<0||event.key>127||event.velocity<1||event.velocity>127)
                    throw std::runtime_error("Invalid performance event "+id);
                events.push_back(event);
            }
            if(events.empty())throw std::runtime_error("Empty performance "+id);
        }
        plugin->releaseResources();
        model->setValueNotifyingHost(float(mi) / (modelCount - 1));
        art->setValueNotifyingHost(float(ai) / 21); gain->setValueNotifyingHost(.6f);
        plugin->setPlayConfigDetails(0, 2, 48000, 256);
        plugin->setNonRealtime(true); plugin->prepareToPlay(48000, 256);
        juce::AudioBuffer<float> result(2, frames);
        for (int start = 0; start < frames; start += 256) {
            int n = std::min(256, frames - start);
            juce::AudioBuffer<float> audio(2, n); juce::MidiBuffer midi;
            for (int strike = 0; strike < repeats; ++strike) {
                int time = 4800 + strike * interval;
                if (time >= start && time < start + n) midi.addEvent(juce::MidiMessage::noteOn(1, key, juce::uint8(velocity)), time - start);
                if (time + 2400 >= start && time + 2400 < start + n) midi.addEvent(juce::MidiMessage::noteOff(1, key), time + 2400 - start);
            }
            for(const auto& e:events){
                if(e.frame>=start&&e.frame<start+n)midi.addEvent(juce::MidiMessage::noteOn(1,e.key,juce::uint8(e.velocity)),e.frame-start);
                if(e.frame+2400>=start&&e.frame+2400<start+n)midi.addEvent(juce::MidiMessage::noteOff(1,e.key),e.frame+2400-start);
            }
            plugin->processBlock(audio, midi);
            for (int ch = 0; ch < 2; ++ch) for (int s = 0; s < n; ++s) {
                auto x = audio.getSample(ch, s);
                if (!std::isfinite(x) || std::abs(x) > 1) throw std::runtime_error("Invalid output " + id);
                result.setSample(ch, start + s, x);
            }
        }
        auto file = directory.getChildFile(id + ".wav");
        if (file.existsAsFile() && !file.deleteFile()) throw std::runtime_error("Cannot replace " + id);
        std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();
        juce::WavAudioFormat wav;
        auto options = juce::AudioFormatWriterOptions().withSampleRate(48000).withNumChannels(2).withBitsPerSample(24);
        auto writer = wav.createWriterFor(stream, options);
        if (!writer || !writer->writeFromAudioSampleBuffer(result, 0, frames)) throw std::runtime_error("Cannot write " + id);
        if (++count % 100 == 0) std::cout << "Rendered " << count << std::endl;
    }
    std::cout << "PASS loaded VST3; rendered " << count << " jobs at 48000 Hz; Output=0.6; latency=" << plugin->getLatencySamples() << std::endl;
    plugin->releaseResources(); return 0;
} catch (const std::exception& e) { std::cerr << e.what() << std::endl; return 1; } }

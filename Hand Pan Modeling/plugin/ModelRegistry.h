#pragma once
#include <array>
#include <vector>
#include <string>
#include <algorithm>

#include <string_view>
struct ArticulationDescriptor { int group; const char* name; std::vector<int> variants; };
enum class InstrumentFamily { Handpan, TongueDrum };
struct ModelDescriptor { const char* id; const char* name; std::vector<ArticulationDescriptor> articulations; const char* hint="Play the keyboard or send MIDI"; InstrumentFamily family=InstrumentFamily::Handpan; };
inline constexpr int modelCount=7;
inline const std::array<ModelDescriptor,modelCount>& modelRegistry(){
 static const std::array<ModelDescriptor,modelCount> models{{
{"orvane","Orvane", {{0,"Centre",{0,1,2}},{8,"Slap",{8,9}},{10,"Knuckle",{}}}},
{"gravik","Gravik", {{12,"Centre",{12,13,14}},{20,"Slap",{20,21}},{22,"Knuckle",{}}}},
{"nuvora","Nuvora", {{24,"Fingertip",{24,25,26,27,28}},{32,"Slap",{32,33,34,35,36}},{37,"Thumb",{37,38,39}}}},
{"virel","Virel", {{46,"Fingertip",{46,47,48,49,50}},{54,"Slap",{54,55,56,57,58}},{59,"Thumb",{59,60,61}}}},
{"serevin","Serevin", {{80,"Flat strike"},{82,"Corner strike"}}},
{"tembrel","Tembrel", {{84,"Centre strike"},{86,"Slap"},{87,"Muted strike"}},"Play the keyboard or send MIDI",InstrumentFamily::TongueDrum},
{"somera","Somera",{{0,"Fingerpad"},{1,"Fingertip / Slap"},{4,"Muted"}},"D3-C5 chromatic range; fixed nine-zone view"},
 }};return models;
}

inline int modelIndexForID(std::string_view id){
    for(int i=0;i<modelCount;++i)if(id==modelRegistry()[size_t(i)].id)return i;
    return -1;
}

inline constexpr int keyswitchBase=12;
inline constexpr std::array<double,modelCount> modelOutputTrim{1.0,1.5313261163569885,1.524364130479413,1.559378404224664,1.7910466861381662,1.2784865195818775,0.392811634943281};

inline int articulationForKeyswitch(int model,int slot){return slot>=0&&slot<int(modelRegistry()[size_t(model)].articulations.size())?slot:-1;}
inline int keyswitchForArticulation(int,int art){return keyswitchBase+art;}

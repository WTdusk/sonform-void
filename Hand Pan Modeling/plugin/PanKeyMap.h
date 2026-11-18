#pragma once
#include "BinaryData.h"
#include <array>
#include <vector>
#include <sstream>
#include <algorithm>

// Read once, before audio starts. Keep key-zone precedence and root transposition.
struct PanKeyMap {
    struct Zone {int root,low,high;};
    std::array<std::vector<Zone>,89> groups;
    PanKeyMap(){
        std::istringstream input(std::string(BinaryData::zones_csv,size_t(BinaryData::zones_csvSize)));
        std::string row;std::getline(input,row);
        while(std::getline(input,row)){
            std::istringstream columns(row);std::array<std::string,4> v;
            for(auto& s:v)std::getline(columns,s,',');
            const int group=std::stoi(v[0]);Zone z{std::stoi(v[1]),std::stoi(v[2]),std::stoi(v[3])};
            // Auxiliary Gu/low-trigger/noise keys are deliberately removed.
            if(group==11||group==83||group==88)continue;
            if(group>=0&&group<=10&&z.low==53&&z.high==53)continue;
            if(group>=12&&group<=22&&z.low==50&&z.high==50)continue;
            if(group==84&&z.high<=58)continue;
            // Source roots are routing coordinates, not the measured pitch.
            if(group<=10){z.low=std::max(53,z.low-12);z.high=std::min(72,z.high-12);}
            if(group>=12&&group<=22){z.low=std::max(50,z.low-12);z.high=std::min(69,z.high-12);}
            if((group>=24&&group<=28)||(group>=32&&group<=44)){z.low=std::max(47,z.low-12);z.high=std::min(66,z.high-12);}
            if((group>=46&&group<=50)||(group>=54&&group<=66)){z.low=std::max(48,z.low-12);z.high=std::min(67,z.high-12);}
            if(group>=84&&group<=87){z.low=std::max(45,z.low-24);z.high=std::min(64,z.high-24);}
            if(group==80||group==81){z.low=std::max(50,z.low-12);z.high=std::min(72,z.high-12);}
            if(group==82){z.low=std::max(57,z.low+36);z.high=std::min(72,z.high+36);}
            if(z.low>z.high)continue;
            auto& zones=groups.at(size_t(group));
            if(std::none_of(zones.begin(),zones.end(),[&](auto a){return a.root==z.root&&a.low==z.low&&a.high==z.high;}))zones.push_back(z);
        }
    }
    int root(int group,int key)const{
        if(group<0||group>=89)return -1;
        for(auto z:groups[size_t(group)])if(key>=z.low&&key<=z.high)return z.root;
        return -1;
    }
    static int sourceKey(int group,int key){
        if(group==82)return key-36;
        if(group>=84&&group<=87)return key+24;
        if(group<=10||(group>=12&&group<=28)||(group>=32&&group<=50)||(group>=54&&group<=66)||group==80||group==81)return key+12;
        return key;
    }
};
inline const PanKeyMap& panKeyMap(){static const PanKeyMap mapping;return mapping;}

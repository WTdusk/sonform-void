#pragma once
namespace handpan {
// Original Serevin Corner source zones. Outside the mapped span, retain nearest-root fallback.
inline int cornerModelKey(int key){
    if(key<17||key>43)return key;
    if(key<=22)return 21;
    if(key<=24)return 24;
    if(key<=26)return 26;
    if(key<=28)return 28;
    if(key==29)return 29;
    if(key<=31)return 31;
    if(key<=34)return 33;
    return 36;
}
}

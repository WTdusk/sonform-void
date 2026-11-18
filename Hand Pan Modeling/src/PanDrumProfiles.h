#pragma once
#include "ModalBankEngine.h"
namespace handpan {
inline void configureHandpanDynamics(ModalBankEngine& engine,const std::array<std::string_view,5>& ids){
    engine.configureDynamics(ids[0],{0,1,2,3,4,5,8,9});
    engine.configureDynamics(ids[1],{12,13,14,15,16,17,20,21});
    engine.configureDynamics(ids[2],{24,25,26,27,28,32,33,34,35,36,37,38,39});
    engine.configureDynamics(ids[3],{46,47,48,49,50,54,55,56,57,58,59,60,61});
    engine.configureDynamics(ids[4],{80,81,82});
}
}

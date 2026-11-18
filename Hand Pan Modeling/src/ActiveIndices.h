#pragma once
#include <array>
#include <algorithm>
#include <cassert>
namespace handpan {
// Fixed storage and ascending order preserve the original summation order.
template<size_t Capacity> class ActiveIndices {
    std::array<unsigned,Capacity> indices{};unsigned count=0;
public:
    void clear(){count=0;}
    bool empty()const{return count==0;}
    unsigned size()const{return count;}
    unsigned operator[](unsigned position)const{return indices[position];}
    void add(unsigned index){assert(count<Capacity);unsigned p=count;while(p>0&&indices[p-1]>index){indices[p]=indices[p-1];--p;}indices[p]=index;++count;}
    void erase(unsigned position){for(unsigned p=position+1;p<count;++p)indices[p-1]=indices[p];--count;}
};
}

//#include"zlog/eviction.h"
#include"zlog/eviction/lru.h"
#include<forward_list>

namespace zlog{

LRU::~LRU(){}

int LRU::cache_get_hit(uint64_t* pos){
    // std::forward_list<uint64_t>::iterator it(pos); // FIX THIS
    // eviction_list.splice_after(eviction_list.before_begin(), eviction_list, it);
    return 0;
}

int LRU::cache_put_miss(uint64_t pos){
    eviction_list.push_front(pos);
    return 0;
}

uint64_t LRU::get_evicted(){
    // auto it = eviction_list.front(); // FIX
    // auto r = *it;
    

    return 0;
}
}
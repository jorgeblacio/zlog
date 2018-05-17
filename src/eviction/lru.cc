//#include"zlog/eviction.h"
#include"zlog/eviction/lru.h"
//#include<list>

namespace zlog{

LRU::~LRU(){}

int LRU::cache_get_hit(uint64_t* pos){
    auto it = eviction_hash_map[*pos];
    eviction_list.splice(eviction_list.begin(), eviction_list, it);
    return 0;
}

int LRU::cache_put_miss(uint64_t pos){
    eviction_list.push_front(pos);
    eviction_hash_map[pos] = eviction_list.begin();
    return 0;
}

uint64_t LRU::get_evicted(){
    auto r = eviction_list.back();
    eviction_hash_map.erase(r);
    eviction_list.pop_back();

    return r;
}
}
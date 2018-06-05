#include<iostream>
#include<string>
#include<unordered_map>
#include<tuple>
#include<iterator>
#include"include/zlog/eviction.h"
#include"include/zlog/cache.h"

namespace zlog{

Cache::~Cache(){}

int Cache::put(uint64_t* pos, const Slice& data){
    if(options.cache_size > 0 && data.size() < options.cache_size && cache_map.find(*pos) == cache_map.end()){ 
        mut.lock();
        eviction->cache_put_miss(*pos);
        zlog_mempool::cache::string pool_data(data.data(), data.size());
        cache_map[*pos] = zlog_mempool::cache::string(pool_data);
        mut.unlock();

        return 0;
    }else{
        return -1;
    }
}

int Cache::get(uint64_t* pos, std::string* data){
    RecordTick(options.statistics, CACHE_REQS);
    auto map_it = cache_map.find(*pos);
    if(map_it != cache_map.end()){
        data->assign((map_it->second).data(), (map_it->second).size());
        mut.lock();        
        eviction->cache_get_hit(pos);
        mut.unlock();
        return 0;
    }else{
        RecordTick(options.statistics, CACHE_MISSES);
        return 1;
    }
}

int Cache::remove(uint64_t* pos){
    if(cache_map.erase(*pos) > 0){
        return 0;    
    }
    return 1;
}
}

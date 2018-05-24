#include<iostream>
#include<string>
#include<unordered_map>
#include<tuple>
#include<iterator>
#include<cstring>
#include"include/zlog/eviction.h"
#include"include/zlog/cache.h"


zlog::Cache::~Cache(){}

int zlog::Cache::put(uint64_t* pos, const char* data){
    if(options.cache_size > 0 &&
        strlen(data) < options.cache_size && 
        cache_map.find(*pos) == cache_map.end()){ 
        // while(options.cache_size - current_cache_use < strlen(data)){
        //     auto evicted = eviction->get_evicted();
        //     current_cache_use -= cache_map[evicted].Length();
        //     cache_map.erase(evicted);
        //     std::cout << "while" << std::endl;
        // }

        eviction->cache_put_miss(*pos);
        cache_map[*pos] = pool_alloc.AllocString(data);
        current_cache_use += strlen(data);

        return 0;
    }else{
        return -1;
    }
}

int zlog::Cache::get(uint64_t* pos, std::string* data){
    RecordTick(options.statistics, CACHE_REQS);
    auto map_it = cache_map.find(*pos);
    if(map_it != cache_map.end()){
        *data = (map_it->second).ToStdString();
        eviction->cache_get_hit(pos);
        return 0;
    }else{
        RecordTick(options.statistics, CACHE_MISSES);
        return 1;
    }
}

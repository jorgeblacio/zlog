#include<iostream>
#include<string>
#include<unordered_map>
#include<tuple>
#include<iterator>
#include"include/zlog/eviction.h"
#include"include/zlog/cache.h"


zlog::Cache::~Cache(){}

int zlog::Cache::put(uint64_t* pos, std::string* data){
    if(options.cache_size > 0 &&
        data->size() < options.cache_size && 
        cache_map.find(*pos) == cache_map.end()){ 
        while(options.cache_size - current_cache_use < data->size()){
            auto evicted = eviction->get_evicted();
            current_cache_use -= cache_map[evicted].size();
            cache_map.erase(evicted);
        }

        eviction->cache_put_miss(*pos);
        cache_map[*pos] = *data;
        current_cache_use += data->size();

        return 0;
    }else{
        return -1;
    }
}

int zlog::Cache::get(uint64_t* pos, std::string* data){
    auto map_it = cache_map.find(*pos);
    if(map_it != cache_map.end()){
        *data = map_it->second;
        eviction->cache_get_hit(pos);
        return 0;
    }else{
        return 1;
    }
}

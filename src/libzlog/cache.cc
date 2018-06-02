#include<iostream>
#include<string>
#include<unordered_map>
#include<tuple>
#include<iterator>
#include"include/zlog/eviction.h"
#include"include/zlog/cache.h"


zlog::Cache::~Cache(){}

int zlog::Cache::put(uint64_t* pos, std::string* data){
    if(options.cache_size > 0 && data->size() < options.cache_size && cache_map.find(*pos) == cache_map.end()){ 
        mut.lock();
        eviction->cache_put_miss(*pos);
        mempool::cache::string pool_data(data->data());
        cache_map[*pos] = mempool::cache::string(pool_data);
        mut.unlock();

        return 0;
    }else{
        return -1;
    }
}

int zlog::Cache::get(uint64_t* pos, std::string* data){
    RecordTick(options.statistics, CACHE_REQS);
    auto map_it = cache_map.find(*pos);
    if(map_it != cache_map.end()){
        char * c = const_cast<char*>((map_it->second).data());
        data->copy(c, (map_it->second).size());
        mut.lock();        
        eviction->cache_get_hit(pos);
        mut.unlock();
        return 0;
    }else{
        RecordTick(options.statistics, CACHE_MISSES);
        return 1;
    }
}

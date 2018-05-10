#include<iostream>
#include<string>
#include<unordered_map>
#include<tuple>
#include<iterator>
#include"include/zlog/eviction.h"
#include"include/zlog/cache.h"


int zlog::Cache::put(uint64_t* pos, std::string* data){
   if( cache_map.find(*pos) == cache_map.end()){ 
                eviction->cache_put_miss(*pos);
                cache_map[*pos] = *data;
                
                if(cache_map.size() > options.cache_size){ //TODO: options size in bytes
                    cache_map.erase(cache_map.find(eviction->get_evicted())); //evict
                }

                return 0;
            }else{
                return -1; //ERROR: Overwrite not allowed
            }
}

int zlog::Cache::get(uint64_t* pos, std::string* data){
    auto map_it = cache_map.find(*pos);
            if(map_it != cache_map.end()){
                *data = map_it->second;
                
                eviction->cache_get_hit(pos);
                return 0;
            }else{
                return 1; //Not found
            }
}
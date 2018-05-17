#pragma once
#include<string>
#include<iostream>
#include<unordered_map>
//#include"zlog/eviction.h"
#include"zlog/eviction/lru.h"
#include"zlog/options.h"

namespace zlog{
class Cache{
    public:
        Cache(const zlog::Options& ops) : options(ops){

            current_cache_use = 0;

            switch(options.eviction){
                case zlog::Eviction::Eviction_Policy::LRU:
                    eviction = new LRU();
                break;
                default:
                    std::cout << "Eviction policy not implemented" << std::endl;   
                break;
            }

        }
        ~Cache();

        int put(uint64_t* pos, std::string* data);
        int get(uint64_t* pos, std::string* data);

    private:
        zlog::Eviction* eviction;
        std::unordered_map<uint64_t, std::string> cache_map;
        const zlog::Options& options;
        int64_t current_cache_use;
};
}
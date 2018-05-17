#pragma once
#include<string>
#include<list>
#include<unordered_map>
#include"zlog/eviction.h"

namespace zlog{

    class LRU: public Eviction{


        public:

            LRU(){}
            ~LRU();
            
            int cache_get_hit(uint64_t* pos) override;
            int cache_put_miss(uint64_t pos) override;
            uint64_t get_evicted() override;

        private:
            std::unordered_map<uint64_t, std::list<uint64_t>::iterator> eviction_hash_map;
            std::list<uint64_t> eviction_list;
    };
}
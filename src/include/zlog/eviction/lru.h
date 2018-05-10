#pragma once
#include<string>
#include<forward_list>
#include"zlog/eviction.h"

namespace zlog{

    class LRU: public Eviction {


        public:

            LRU(){}
            ~LRU();
            
            int cache_get_hit(uint64_t* pos) override;
            int cache_put_miss(uint64_t pos) override;
            uint64_t get_evicted() override;

        private:
            std::forward_list<uint64_t> eviction_list;
    };
}
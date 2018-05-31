#pragma once
#include<string>
#include<sstream>
#include<iostream>
#include<unordered_map>
#include<mutex>
#include"zlog/eviction/lru.h"
#include"zlog/eviction/arc.h"
#include"zlog/options.h"
#include"zlog/mempool/mempool.h"
#include <CivetServer.h>

namespace zlog{
class Cache{
    public:
        Cache(const zlog::Options& ops) : options(ops),
            metrics_http_server_({"listening_ports", "0.0.0.0:8080", "num_threads", "1"}),
            metrics_handler_(this){

            metrics_http_server_.addHandler("/metrics", &metrics_handler_);

            current_cache_use = 0;

            switch(options.eviction){
                case zlog::Eviction::Eviction_Policy::LRU:
                    eviction = new LRU(options.cache_size, this);
                break;
                case zlog::Eviction::Eviction_Policy::ARC:
                    eviction = new ARC(options.cache_size, this);
                break;
                default:
                    std::cout << "Eviction policy not implemented" << std::endl;   
                break;
            }

        }
        ~Cache();

        int put(uint64_t* pos, std::string* data);
        int get(uint64_t* pos, std::string* data);

        std::unordered_map<uint64_t, mempool::cache::string> cache_map; //FIX
       

    private:

        class MetricsHandler : public CivetHandler {
            public:
                explicit MetricsHandler(Cache* c) : c_(c){}

                bool handleGet(CivetServer *server, struct mg_connection *conn) {

                    zlog::Statistics* stats = c_->options.statistics;

                    std::stringstream out_stream;

                    out_stream << stats->ToString() << std::endl;

                    std::string body = out_stream.str();
                    std::string content_type = "text/plain";

                    mg_printf(conn,
                    "HTTP/1.1 200 OK\r\n"
                    "Content-Type: %s\r\n",
                    content_type.c_str());
                    
                    mg_printf(conn, "Content-Length: %lu\r\n\r\n",
                    static_cast<unsigned long>(body.size()));
                    
                    mg_write(conn, body.data(), body.size());

                    return true;
                }

                Cache* c_;
        };


    private:
        zlog::Eviction* eviction;
        const zlog::Options& options;
        int64_t current_cache_use;
        std::mutex mut;

        CivetServer metrics_http_server_;
        MetricsHandler metrics_handler_;
};
}
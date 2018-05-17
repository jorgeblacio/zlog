#include<iostream>
#include<sstream>
#include<random>
#include<chrono>
#include <ctime>
#include <ratio>
#include"zlog/log.h"

const int TEST_SIZE = 10000000;

int main(int argc, char** argv){

    if(argc < 2){
        std::cout << "Specify cache size" << std::endl;
        std::cout << "Usage: ./cache_test CACHE_SIZE" << std::endl;        
        std::cout << "e.g: ./cache_test 2048" << std::endl; 
        return -1;
    } 
    

    zlog::Options options;

    std::istringstream iss( argv[1] );
    int val;

    if (iss >> val && val >= 0){
        options.cache_size = val;
    }else{
        std::cout << "Invalid cache size" << std::endl;        
        std::cout << "Usage: ./cache_test CACHE_SIZE" << std::endl;        
        std::cout << "e.g: ./cache_test 2048" << std::endl;        
        return -1;
    }


    zlog::Log *log;
    int ret = zlog::Log::Open(options, "lmdb", "mylog",
        {{"path", "/tmp/zlog.tmp.db"}}, "", "", &log);
    assert(ret == 0);

    assert(ret == 0);
    (void)ret;

    std::string output;

    std::default_random_engine generator;
    std::binomial_distribution<int> distribution(10000,0.5);

    std::vector<uint64_t> test_vector(TEST_SIZE);

    //generate random values
    for(int i = 0; i < TEST_SIZE; ++i){

        test_vector[i] = distribution(generator);
        //std::cout << rnd << std::endl;
    }


    std::chrono::high_resolution_clock::time_point t1 = std::chrono::high_resolution_clock::now();
    for(int i = 0; i < TEST_SIZE; ++i){
        ret = log->Read(test_vector[i], &output);
    }
    std::chrono::high_resolution_clock::time_point t2 = std::chrono::high_resolution_clock::now();

    
    std::chrono::duration<double> time_span = std::chrono::duration_cast<std::chrono::duration<double>>(t2 - t1);
    std::cout << time_span.count() << " seconds." << std::endl;

    assert(ret == 0);

}
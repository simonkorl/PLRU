#include <iostream>
#include <cstring>
#include <stdlib.h>
#include "CacheSim.h"
#include "argparse.hpp"

using namespace std;

int main(const int argc, const char *argv[]) {

    //以下为固定的测试
    ArgumentParser parser;

    parser.addArgument("-i", "--input", 1, false);//trace的地址
    parser.addArgument("--l1", 1, true);
    parser.addArgument("--l2", 1, true);
    parser.addArgument("--line_size", 1, true);
    parser.addArgument("--ways", 1, true);
    parser.parse(argc, argv);

    CacheSim cache;
    /**cache有关的参数*/
    _u64 line_size[] = {64}; //cache line 大小：64B
    //_u64 ways[] = {16,32}; //组相联的路数
    _u64 ways[] = {32}; //组相联的路数
    //_u64 cache_size[] = {0x400000,0x800000};//cache大小:4M, 8M
    _u64 cache_size[] = {0x400000};//cache大小:4M
    //cache_swap_style swap_style[] = {CACHE_SWAP_LRU};
    cache_swap_style swap_style[] = {CACHE_SWAP_PLRU};
    //cache_swap_style swap_style[] = {CACHE_SWAP_RAND, CACHE_SWAP_LRU, CACHE_SWAP_FRU, CACHE_SWAP_SRRIP, CACHE_SWAP_SRRIP_FP, CACHE_SWAP_BRRIP, CACHE_SWAP_DRRIP, CACHE_SWAP_PLRU};
    int ms[] = {3};

    int i, j, m, k, n;
    for (m = 0; m < sizeof(cache_size) / sizeof(_u64); m++) {//仅执行一次，表示缓存有4M。因为cache_size数组长度为1，代表只有一种模拟类型
        for (i = 0; i < sizeof(line_size) / sizeof(_u64); i++) {//仅执行一次，表示cache line的大小为 64B 。原因同上
            for (j = 0; j < sizeof(ways) / sizeof(_u64); j++) {//仅执行一次，表示32路组相连
                for (k = 0; k < sizeof(ms) / sizeof(int); k++) {//仅执行一次，表示SRRIP的参数M的值为3.
                   for (n = 0; n < sizeof(swap_style) / sizeof(cache_swap_style); n++) {
                        _u64 temp_cache_size, temp_line_size, temp_ways;
                        cache_swap_style temp_swap_style;

                        temp_cache_size = cache_size[m];
                        temp_line_size = line_size[i];
                        temp_ways = ways[j];
                        temp_swap_style = swap_style[n];
                        cache.init(temp_cache_size, temp_line_size, temp_ways, temp_swap_style);
                        cache.set_M(ms[k]);
                        cache.load_trace(parser.retrieve<string>("input").c_str());
                        cache.re_init();
                    }
                }
            }
        }
    }
    return 0;
}

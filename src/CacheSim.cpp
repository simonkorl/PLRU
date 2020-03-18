//
// Created by find on 16-7-19.
// Cache architect
// memory address  format:
// |tag|组号 log2(组数)|组内块号log2(mapping_ways)|块内地址 log2(cache line)|
//
#include "CacheSim.h"
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <cstdio>
#include <time.h>
#include <climits>
#include "map"
#include <fstream>
#include <vector>
#include <algorithm>
#include <string>

static inline _u64 get_ulonglong_max(void)
{
#ifdef __MACH__	
	return ULONG_MAX;
#else
	return ULONG_LONG_MAX;
#endif
}

CacheSim::CacheSim() {}

/**@arg a_cache_size[] 多级cache的大小设置
 * @arg a_cache_line_size[] 多级cache的line size（block size）大小
 * @arg a_mapping_ways[] 组相连的链接方式*/

_u32 CacheSim::pow_int(int base, int expontent) {
    _u32 sum = 1;
    for (int i = 0; i < expontent; i++) {
        sum *= base;
    }
    return sum;
}

_u64 CacheSim::pow_64(_u64 base, _u64 expontent) {
    _u64 sum = 1;
    for (int i = 0; i < expontent; i++) {
        sum *= base;
    }
    return sum;
}

void CacheSim::set_M(int m) {
    SRRIP_M = m;
    SRRIP_2_M_1 = pow_int(2, SRRIP_M) - 1;
    SRRIP_2_M_2 = pow_int(2, SRRIP_M) - 2;
}

void CacheSim::init(_u64 a_cache_size, _u64 a_cache_line_size, _u64 a_mapping_ways, cache_swap_style a_swap_style) {
   //如果输入配置不符合要求
    if (a_cache_line_size < 0 || a_mapping_ways < 1 ) {
        printf("cache line size or mapping ways are illegal\n");
        return;
    }

    //cache replacement policy
    swap_style = a_swap_style;

    // 几路组相联
    cache_mapping_ways = a_mapping_ways;

    //Cache Line大小
    cache_line_size = a_cache_line_size;

    //Cache大小
    cache_size = a_cache_size;

    // 总的line数 = cache总大小/ 每个line的大小（一般64byte，模拟的时候可配置）
    cache_line_num = (_u64) a_cache_size / a_cache_line_size;
    cache_line_shifts = (_u64) log2(a_cache_line_size);

    // 总共有多少set
    cache_set_size = cache_line_num / cache_mapping_ways;
    // 其二进制占用位数，同其他shifts
    cache_set_shifts = (_u64) log2(cache_set_size);


    cache_r_count = 0;
    cache_w_count = 0;
    cache_hit_count = 0;
    cache_miss_count = 0;
    cache_w_memory_count = 0;
    cache_r_memory_count = 0;
    cache_r_hit_count = 0;
    cache_w_hit_count = 0;
    cache_w_miss_count = 0;
    cache_r_miss_count = 0;

    // ticktock，主要用来在替换策略的时候提供比较的key，在命中或者miss的时候，相应line会更新自己的count为当时的tick_count;
    tick_count = 0;
    
    // 为每一行分配空间
    caches = (Cache_Line *) malloc(sizeof(Cache_Line) * cache_line_num);
    memset(caches, 0, sizeof(Cache_Line) * cache_line_num);

    for (int i = 0; i < cache_set_size; ++i) 
        for (int j = 0; j < cache_mapping_ways; ++j) 
            caches[i * cache_mapping_ways + j].LRU = j;

    //用于SRRIP算法
    PSEL = 0;
    cur_win_replace_policy = CACHE_SWAP_SRRIP;

    write_through = 0;
    write_allocation = 1;

    // //用于LIRS算法
    Lhirs = int(0.01f * cache_mapping_ways);
    Lhirs = Lhirs == 0 ? 1 : Lhirs;

    Llirs = cache_mapping_ways - Lhirs;

    //stacks = new std::vector<_u32>[cache_line_num];
    // queues = new std::deque<_u32>[cache_line_num];

    // MAX_QUEUE_SIZE = cache_mapping_ways;
    // MIN_QUEUE_SIZE = Lhirs;
    
}
// /**在栈底的LIR被删除后，通过该函数来清除栈底所有的HIR*/
// void CacheSim::lirs_stack_prune(_u64 set_base, std::vector<_u32>& stack){
//     for(auto iter = stack.begin(); iter != stack.end(); iter++){
//         if(caches[set_base + *iter].IS_LIR){
//             stack.erase(stack.begin(), iter);
//             break;
//         }
//     }
    
// }
/**顶部的初始化放在最一开始，如果中途需要对tick_count进行清零和caches的清空，执行此。*/
void CacheSim::re_init() {
    tick_count = 0;
    cache_hit_count = 0;
    cache_miss_count = 0;
    memset(caches, 0, sizeof(Cache_Line) * cache_line_num);

    //delete[] stacks;
    //delete[] queues;
}

CacheSim::~CacheSim() {
    free(caches);
}

int CacheSim::cache_check_hit(_u64 set_base, _u64 addr) {
    /**循环查找当前set的所有way（line），通过tag匹配，查看当前地址是否在cache中*/
    _u64 i;
    for (i = 0; i < cache_mapping_ways; ++i) {
        if ((caches[set_base + i].flag & CACHE_FLAG_VALID) &&
            (caches[set_base + i].tag == ((addr >> (cache_set_shifts + cache_line_shifts))))) {
            return i; //返回line在set内的偏移地址
        }
    }
    return -1;
}

/**获取当前set中INVALID的line*/
int CacheSim::cache_get_free_line(_u64 set_base) {
    for (_u64 i = 0; i < cache_mapping_ways; ++i) {
        if (!(caches[set_base + i].flag & CACHE_FLAG_VALID)) {
            return i;
        }
    }
   return -1;
}
// _u32 CacheSim::get_lirs_num(_u64 set_base, std::vector<_u32>& stack){
//     _u32 lir_count = 0;
//     for(auto iter = stack.begin(); iter != stack.end(); ++iter){
//          if(caches[set_base + *iter].IS_LIR) lir_count++;
//     }
//     return lir_count;
// }
/**Normal Cache命中，更新Cache状态*/
void CacheSim::cache_hit(_u64 set_base, _u64 index, int a_swap_style) {

        switch (a_swap_style) {
                case CACHE_SWAP_LRU:
                    for (_u64 j = 0; j < cache_mapping_ways; ++j) {
                        if ((caches[set_base + j].LRU < caches[set_base + index].LRU) && (caches[set_base + j].flag & CACHE_FLAG_VALID)) {
                            caches[set_base + j].LRU ++;
                        }
                    }
                    caches[set_base + index].LRU = 0;
                    break;
                case CACHE_SWAP_FRU:
                    caches[set_base + index].FRU++; 
                    break;
                case CACHE_SWAP_BRRIP:
                case CACHE_SWAP_SRRIP:
                    caches[set_base + index].RRPV = 0;
                    break;
                case CACHE_SWAP_SRRIP_FP:
                    if (caches[set_base + index].RRPV != 0) {
                        caches[set_base + index].RRPV -= 1;
                    }
                    break;
                case CACHE_SWAP_PLRU:
                    int pcount = 0;
                    int max_plru = 0;
                    int max_index = -1;
                    for(int i = 0;i < cache_mapping_ways; ++i){
                        if(index != i && caches[set_base + i].P == true){
                            caches[set_base + i].PLRU ++;
                            if(caches[set_base + i].PLRU > max_plru){
                                max_plru = caches[set_base + i].PLRU;
                                max_index = i;
                            }
                        }
                        if(caches[set_base + i].P == true){
                            pcount ++;
                        }
                    }
                    if(caches[set_base + index].P == false){
                        caches[set_base + index].P = true;
                        caches[set_base + index].PLRU = 0;
                        caches[set_base + index].Q = 0;
                        if(pcount == Llirs){
                            caches[set_base + max_index].Q = 1;
                            caches[set_base + max_index].P = false;
                            caches[set_base + max_index].PLRU = UINT32_MAX;
                            for(int i = 0;i < cache_mapping_ways; ++i){
                                if(i != max_index && caches[set_base + i].Q > 0){
                                    caches[set_base + i].Q++;
                                }
                            } 
                        }
                    }else{
                        caches[set_base + index].PLRU = 0;
                        caches[set_base + index].Q = 0;
                    }
                    break;
                    
                // case CACHE_SWAP_LIRS:
                //     Cache_Line& c_cache = caches[set_base + index];
                //     std::vector<_u32>& c_stack = stacks[set_base];
                //     if(c_cache.IS_LIR){
                //         // 如果是LIR，将其移动至栈顶，并且裁剪
                //         for(auto iter = c_stack.begin(); iter != c_stack.end(); ++iter){
                //             if(*iter == index){
                //                 c_stack.erase(iter);
                //             }else{
                //                 caches[set_base + *iter].R++;
                //             }
                //         }
                //         c_stack.push_back(index);
                //         c_cache.R = 0;

                //         lirs_stack_prune(set_base, c_stack);
                //     }else{
                //         auto c_queue = queues[set_base];
                //         bool is_exist = false;
                //         auto index_iter = c_stack.end();
                //         // 遍历栈寻找当前块，并且将栈中其他元素的R值增加1
                //         for(auto iter = c_stack.begin(); iter != c_stack.end(); ++iter){
                //             if(*iter == index){
                //                 is_exist = true;
                //                 index_iter = iter;
                //             }else{
                //                 caches[set_base + *iter].R++;
                //             }
                //         }
                //         // 如果在栈中有，则从栈中移动至栈顶，否则直接加入栈顶
                //         if(is_exist){
                //             c_stack.erase(index_iter);
                //             c_stack.push_back(index);
                //         }else{
                //             c_stack.push_back(index);
                //         }
                //         c_cache.R = 0;
                //         // 切换为LIR
                //         c_cache.IS_LIR = true;
                //         // 从队列中删除之
                //         for(auto iter = c_queue.begin(); iter != c_queue.end(); ++iter){
                //             if(*iter == index){
                //                 c_queue.erase(iter);
                //                 break;
                //             }
                //         }
                //         // 如果此时LIR太多了，则把栈底的LIR转变为 HIR，并且加入队列
                //         // 栈裁剪
                //         if(get_lirs_num(set_base, c_stack) > Llirs){
                //             auto flir = c_stack.erase(c_stack.begin());
                //             caches[set_base + *flir].IS_LIR = false;
                //             queues[set_base].push_back(*flir);
                //             lirs_stack_prune(set_base, c_stack);
                //         }
                //     }
                //     break;
        }
}

/**缺失，更新index指向的cache行状态*/
void CacheSim::cache_insert(_u64 set_base, _u64 index, int a_swap_style) {
        double rand_num; 
        switch (a_swap_style) {
                case CACHE_SWAP_LRU:
                    for (_u64 j = 0; j < cache_mapping_ways; ++j) {
                        if ((caches[set_base + j].LRU < caches[set_base + index].LRU) && (caches[set_base + j].flag & CACHE_FLAG_VALID)) {
                            caches[set_base + j].LRU ++;
                        }
                    }
                    caches[set_base + index].LRU = 0;
                    break;
                case CACHE_SWAP_FRU:
                    caches[set_base + index].FRU = 1; 
                    break;
                case CACHE_SWAP_SRRIP_FP:
                case CACHE_SWAP_SRRIP:
                    caches[set_base + index].RRPV = SRRIP_2_M_2;
                    break;
                case CACHE_SWAP_BRRIP:
                    rand_num =  (double)rand() / RAND_MAX;
                    caches[set_base + index].RRPV = (rand_num > EPSILON)? SRRIP_2_M_1 : SRRIP_2_M_2;
                    break;
                case CACHE_SWAP_PLRU:
                    caches[set_base + index].Q = 1;
                    caches[set_base + index].P = false;
                    caches[set_base + index].PLRU = UINT32_MAX;
                    for(int i = 0;i < cache_mapping_ways; ++i){
                        if(i != index && caches[set_base + i].Q > 0){
                            caches[set_base + i].Q++;
                        }
                    }
                    // decay 算法
                    int max_plru = 0;
                    int max_index = -1;
                    for(int i = 0;i < cache_mapping_ways; ++i){
                        if(caches[set_base + i].P == true && caches[set_base + i].PLRU > max_plru){
                            max_plru = caches[set_base + i].PLRU;
                            max_index = i;
                        }
                    }
                    if(max_plru > 100 * Lhirs){
                        caches[set_base + max_index].Q = 1;
                        caches[set_base + max_index].P = false;
                        caches[set_base + max_index].PLRU = UINT32_MAX;
                        for(int i = 0;i < cache_mapping_ways; ++i){
                            if(i != max_index && caches[set_base + i].Q > 0){
                                caches[set_base + i].Q++;
                            }
                        }
                    }
                    break;
                // case CACHE_SWAP_LIRS:
                //     std::vector<_u32>& c_stack = stacks[set_base];
                //     std::deque<_u32>& c_queue = queues[set_base];
                //     Cache_Line c_cache = caches[set_base + index];
                //     bool is_in_stack = false;
                //     bool is_in_queue = false;
                //     for(auto iter = c_stack.begin(); iter != c_stack.end(); ++iter){
                //         if(*iter == index){
                //             // 如果已经在栈中，则需要更新信息
                //             // 将其移动至栈顶，并且增加其它元素的 R
                //             c_stack.erase(iter);
                //             c_stack.push_back(index);
                //             c_cache.IS_LIR = false;
                //             c_cache.R = 0;

                //             is_in_stack = true;
                //         } else{
                //             caches[set_base + *iter].R ++;
                //         }
                //     }
                //     for(auto iter = c_queue.begin(); iter != c_queue.end(); ++iter){
                //         if(*iter == index){
                //             // 如果已经在队列中，则将其移动至队尾
                //             c_queue.erase(iter);
                //             c_queue.push_back(index);

                //             is_in_queue = true;
                //             break;
                //         }
                //     }
                //     if(!is_in_stack){
                //         c_stack.push_back(index);
                //         c_cache.IS_LIR = false;
                //         c_cache.R = 0;
                //     }
                //     if(!is_in_queue){
                //         c_queue.push_back(index);
                //     }

                //     // decay机制，如果栈底的若干个 LIR 间隔的不同数据太多，便将它们降级为 HIR
                //     std::vector<_u32>::iterator decay_end = c_stack.end();
                //     for(auto iter = c_stack.begin(); iter != c_stack.end(); ++iter){
                //         if(caches[set_base + *iter].IS_LIR && caches[set_base + *iter].R >= 100 * c_queue.size()){
                //             caches[set_base + *iter].IS_LIR = false;
                //             queues[set_base].push_back(*iter);
                //             decay_end = iter;
                //         }
                //     }
                //     if(decay_end != c_stack.end()){
                //         c_stack.erase(c_stack.begin(),++decay_end);
                //     }
                //     break;
        }
}

/**获取当前set中可用的line，如果没有，就找到要被替换的块*/
int CacheSim::cache_find_victim(_u64 set_base , int a_swap_style, int hit_index) {
    int free_index;//要替换的cache line
    _u64 min_FRU;
    _u64 max_LRU;

    /**从当前cache set里找可用的空闲line */
    free_index = cache_get_free_line(set_base); 
    if (free_index >= 0) {
         return free_index;
    }

    /**没有可用line，则执行替换算法*/
    free_index = -1;
    switch (a_swap_style) {
        case CACHE_SWAP_RAND:
            free_index = rand() % cache_mapping_ways;
            break;
        case CACHE_SWAP_LRU:
            max_LRU = 0;
            for (_u64 j = 0; j < cache_mapping_ways; ++j) {
                if (caches[set_base + j].LRU > max_LRU) {
                    max_LRU = caches[set_base + j].LRU; 
                    free_index = j;
                }
            }
            break;
       case CACHE_SWAP_FRU:
            min_FRU = get_ulonglong_max();
            for (_u64 j = 0; j < cache_mapping_ways; ++j) {
                if (caches[set_base + j].FRU < min_FRU) {
		            min_FRU = caches[set_base + j].FRU; 
                    free_index = j;
                }
            }
            break;
        case CACHE_SWAP_SRRIP:
        case CACHE_SWAP_SRRIP_FP:
        case CACHE_SWAP_BRRIP:
            while (free_index < 0) {
                for (_u64 j = 0; j < cache_mapping_ways; ++j) {
                    if (caches[set_base + j].RRPV == SRRIP_2_M_1) {
                        free_index = j;
                        break;
                    }
                }
                // increment all RRPVs
                if (free_index < 0) {
                    // increment all RRPVs
                    for (_u64 j = 0; j < cache_mapping_ways; ++j) {
                        caches[set_base + j].RRPV++;
                    }
                } else {
                    break;
                }
            }
            break;
        case CACHE_SWAP_PLRU:
            int max_index = -1;
            int max_q = 0;
            for(int i = 0;i < cache_mapping_ways; ++i){
                if(caches[set_base + i].Q > 0){
                    if(caches[set_base + i].Q > max_q){
                        max_index = i;
                        max_q = caches[set_base + i].Q;
                    }
                }
            }
            if(max_index != -1)
                free_index = max_index;
            else{
                int max_plru = 0;
                for(int i = 0;i < cache_mapping_ways; ++i){
                    if(caches[set_base + i].PLRU > 0){
                        if(caches[set_base + i].PLRU > max_plru){
                            max_index = i;
                            max_plru = caches[set_base + i].PLRU;
                        }
                    }
                }
            }
            free_index = max_index;
            break;
        // case CACHE_SWAP_LIRS:
        //     if(queues[set_base].size() > 0){
        //         return queues[set_base].front();
        //     } else{
        //         printf("error: A victim is searched in an empty queue.\n");
        //     }
        //     break;
   }

   if (free_index >= 0) {
        //如果原有的cache line是脏数据，标记脏位
        if (caches[set_base + free_index].flag & CACHE_FLAG_DIRTY) {
            // TODO: 写回到L2 cache中。
            caches[set_base + free_index].flag &= ~CACHE_FLAG_DIRTY;
            cache_w_memory_count++;
        }
    } else {
        printf("I should not show\n");
    }
    return free_index;
}

/**返回这个set是否是sample set。*/
int CacheSim::get_set_flag(_u64 set_base) {
    // size >> 10 << 5 = size * 32 / 1024 ，参照论文中的sample比例

    int K = cache_set_size >> 5;
    int log2K = (int) log2(K);
    int log2N = (int) log2(cache_set_size);
    // 使用高位的几位，作为筛选.比如需要32 = 2^5个，则用最高的5位作为mask
    _u64 mask = pow_64(2, (_u64) (log2N - log2K)) - 1;
    _u64 residual = set_base & mask;
    return residual;
}

double CacheSim::get_miss_rate() {
    return 100.0 * cache_miss_count / (cache_miss_count + cache_hit_count);
}

double CacheSim::get_hit_rate() {
    return 100.0 * cache_hit_count / (cache_miss_count + cache_hit_count);
}

void CacheSim::dump_cache_set_info(_u64 set_base) {
    int i;
    printf("Ways: ");
    for (i = 0; i < cache_mapping_ways; ++i) {
        printf("%6d", i);
    }
    printf("\nTags: ");
    for (i = 0; i < cache_mapping_ways; ++i) {
        printf("%6llx", caches[set_base+i].tag);
    }
    printf("\nValid:");
    for (i = 0; i < cache_mapping_ways; ++i) {
        if (caches[set_base + i].flag & CACHE_FLAG_VALID)
            printf("%6d", 1);
        else
            printf("%6d", 0);
    }
    printf("\nLRU:  ");
    for (i = 0; i < cache_mapping_ways; ++i) {
        printf("%6u", caches[set_base + i].LRU);
    }
    printf("\nFRU:  ");
    for (i = 0; i < cache_mapping_ways; ++i) {
        printf("%6u", caches[set_base + i].FRU);
    }
    printf("\nRRPV: ");
    for (i = 0; i < cache_mapping_ways; ++i) {
        printf("%6u", caches[set_base + i].RRPV);
    }
    printf("\n");
}

/**不需要分level*/
void CacheSim::do_cache_op(_u64 addr, char oper_style) {
    _u64 set, set_base;
    int hit_index, free_index;

    tick_count++;
    if (oper_style == OPERATION_READ) cache_r_count++;
    if (oper_style == OPERATION_WRITE) cache_w_count++;

    //normal cache and sample cache has the same set number
    set = (addr >> cache_line_shifts) % cache_set_size; 
    set_base = set * cache_mapping_ways; //cache内set的偏移地址 0 8 16 ...
    hit_index = cache_check_hit(set_base, addr);//set内line的偏移地址 0-7

    int temp_swap_style = swap_style;
    int set_flag = get_set_flag(set); //返回当前set是否为sample set
    if (swap_style == CACHE_SWAP_DRRIP) {
        /**是否是sample set*/
        switch (set_flag) {
            case 0:
                temp_swap_style = CACHE_SWAP_BRRIP;
                break;
            case 1:
                temp_swap_style = CACHE_SWAP_SRRIP;
                break;
            default:
                if (PSEL > 0) {
                    cur_win_replace_policy = CACHE_SWAP_BRRIP;
                } else {
                    cur_win_replace_policy = CACHE_SWAP_SRRIP;
                }
                temp_swap_style = cur_win_replace_policy;
        }
    }

    /*
    if (set==0)
        dump_cache_set_info(set_base);
    */

    //是否写操作
    if (oper_style == OPERATION_WRITE) {
        if (hit_index >= 0) {
            //命中Cache
            cache_w_hit_count++;
            cache_hit_count++;
            if (write_through) {
                cache_w_memory_count++;
            } else {
                caches[set_base + hit_index].flag |= CACHE_FLAG_DIRTY;
            }
            //更新替换相关状态位
            cache_hit(set_base, hit_index, temp_swap_style);
        } else {
            cache_w_miss_count++;
            cache_miss_count++;

            if (write_allocation) {
                //写操作需要先从memory读到cache，然后再写
                cache_r_memory_count++;
                
                //找一个victim，从Cache中替换出
                free_index = cache_find_victim(set_base, temp_swap_style, hit_index);
                //写入新的cache line
                caches[set_base + free_index].tag = addr >> (cache_set_shifts + cache_line_shifts);
                caches[set_base + free_index].flag = (_u8) ~CACHE_FLAG_MASK;
                caches[set_base + free_index].flag |= CACHE_FLAG_VALID;
                if (write_through) {
                     cache_w_memory_count++;
                 } else {
                        // 只是置脏位，不用立刻写回去，这样如果下次是write hit，就可以减少一次memory写操作
                        caches[set_base + free_index].flag |= CACHE_FLAG_DIRTY;
                 }

                 //更新替换相关的状态位
                 cache_insert(set_base, free_index, temp_swap_style);

                // 如果是动态策略，则还需要更新psel(psel>0, select BRRIP; PSEL<=0, select SRRIP)
                int set_flag = get_set_flag(set);
                if (swap_style == CACHE_SWAP_DRRIP) {
                    if (set_flag == 1) { //if it is SRRIP now, psel++, tend to select BRRIP
                        PSEL++;
                    } else if (set_flag == 0) { //if it is BRRIP now, psel++, tend to select SRRIP
                        PSEL--;
                    }
                }
            } else {
                cache_w_memory_count++;
            }
        }
    } else {
        if (hit_index >= 0) {
            //命中实际Cache
            cache_r_hit_count++;
            cache_hit_count++;

            //更新替换相关状态位
            cache_hit(set_base, hit_index, temp_swap_style);
            
        } else {
            cache_r_miss_count++;
            cache_r_memory_count++;
            cache_miss_count++;

            //找一个victim，从Cache中替换出
            free_index = cache_find_victim(set_base, temp_swap_style, hit_index);
            //写cache line
            caches[set_base + free_index].tag = addr >> (cache_set_shifts + cache_line_shifts);
            caches[set_base + free_index].flag = (_u8) ~CACHE_FLAG_MASK;
            caches[set_base + free_index].flag |= CACHE_FLAG_VALID;

            //更新替换相关的状态位
            cache_insert(set_base, free_index, temp_swap_style);

            // 如果是动态策略，则还需要更新psel(psel>0, select BRRIP; PSEL<=0, select SRRIP)
            int set_flag = get_set_flag(set);
            if (swap_style == CACHE_SWAP_DRRIP) {
                if (set_flag == 1) { //if it is SRRIP now, psel++, tend to select BRRIP
                    PSEL++;
                } else if (set_flag == 0) { //if it is BRRIP now, psel++, tend to select SRRIP
                    PSEL--;
                }
            }
        }
    }
}

/**从文件读取trace，在我最后的修改目标里，为了适配项目，这里需要改掉*/
void CacheSim::load_trace(const char *filename) {
    char buf[128];
    // 添加自己的input路径
    FILE *fin;
    // 记录的是trace中指令的读写，由于cache机制，和真正的读写次数可能不一样。主要是如果设置的写回法，则被写的脏cache line会等在cache中，直到被替换。
    _u64 rcount = 0, wcount = 0;

    /**读取cache line，做cache操作*/
    fin = fopen(filename, "r");
    if (!fin) {
        printf("load_trace %s failed\n", filename);
        return;
    }
    _u64 i = 0;

    while (fgets(buf, sizeof(buf), fin)) {
        char tmp_style[5];
        char style;

        _u64 addr = 0;
        int datalen = 0;
        int burst = 0;
        int mid = 0;
        float delay = 0;
        float ATIME = 0;
        int ch = 0;
        int qos = 0;

        sscanf(buf, "%s %llx %d %d %x %f %f %d %d", tmp_style, &addr, &datalen, &burst, &mid, &delay, &ATIME, &ch, &qos);
        if (strcmp(tmp_style, "nr") == 0 || strcmp(tmp_style, "wr") == 0) {
            style = 'l';
        } else if (strcmp(tmp_style, "nw") == 0 || strcmp(tmp_style, "naw") == 0) {
            style = 's';
        } else {
            printf("%s", tmp_style);
            return;
        }

        //访问cache
        do_cache_op(addr, style);

        switch (style) {
            case 'l' :
                rcount++;
                break;
            case 's' :
                wcount++;
                break;
            case 'k' :
                break;
            case 'u' :
                break;
        }
        i++;
    }

    printf("\n========================================================\n");
    printf("cache_size: %lld, cache_line_size:%lld, cache_set_size:%lld, mapping_ways:%lld, ", cache_size, cache_line_size, cache_set_size, cache_mapping_ways);
    char a_swap_style[99];
    switch (swap_style) {
        case CACHE_SWAP_RAND:
            strcpy(a_swap_style, "RAND");
            break;
        case CACHE_SWAP_LRU:
            strcpy(a_swap_style, "LRU");
            break;
        case CACHE_SWAP_FRU:
            strcpy(a_swap_style, "FRU");
            break;
        case CACHE_SWAP_SRRIP:
            strcpy(a_swap_style, "SRRIP");
            break;
        case CACHE_SWAP_SRRIP_FP:
            strcpy(a_swap_style, "SRRIP_FP");
            break;
        case CACHE_SWAP_BRRIP:
            strcpy(a_swap_style, "BRRIP");
            break;
        case CACHE_SWAP_DRRIP:
            strcpy(a_swap_style, "DRRIP");
            break;
        case CACHE_SWAP_PLRU:
            strcpy(a_swap_style, "PLRU");
            break;
        // case CACHE_SWAP_LIRS:
        //     strcpy(a_swap_style, "LIRS");
        //     break;
    }
    printf("cache_repl_policy:%s", a_swap_style);
    if (swap_style == CACHE_SWAP_SRRIP) {
        printf("\t%d", SRRIP_M);
    }
    printf("\n");
 
    // 文件中的指令统计
    printf("all r/w/sum: %lld %lld %lld \nall in cache r/w/sum: %lld %lld %lld \nread rate: %f%%\twrite rate: %f%%\nread rate in cache: %f%%\twrite rate in cache: %f%%\n",
           rcount, wcount, rcount+wcount,
           cache_r_count, cache_w_count, tick_count,
           100.0 * rcount / (rcount+wcount),
           100.0 * wcount / (rcount+wcount),
           100.0 * cache_r_count / tick_count,
           100.0 * cache_w_count / tick_count
    );
    printf("Cache hit/miss: %lld/%lld\t hit/miss rate: %f%%/%f%%\n",
           cache_hit_count, cache_miss_count,
           100.0 * cache_hit_count / (cache_hit_count + cache_miss_count),
           100.0 * cache_miss_count / (cache_hit_count + cache_miss_count));
    printf("read hit count in cache %lld\tmiss count in cache %lld\nwrite hit count in cache %lld\tmiss count in cache %lld\n", cache_r_hit_count, cache_r_miss_count, cache_w_hit_count, cache_w_miss_count);
    printf("Write through:\t%d\twrite allocation:\t%d\n", write_through, write_allocation);
    printf("Memory --> Cache:\t%.4fGB\nCache --> Memory:\t%.4fMB\ncache sum:\t%.4fGB\n",
           cache_r_memory_count * cache_line_size * 1.0 / 1024 / 1024 / 1024,
           cache_w_memory_count * cache_line_size * 1.0 / 1024 / 1024,
           (cache_r_memory_count * cache_line_size * 1.0 / 1024 / 1024 / 1024)+cache_w_memory_count * cache_line_size * 1.0 / 1024 / 1024/ 1024); //cache sum：经过cache操作的流量总和
    
    fclose(fin);
}


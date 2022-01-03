#ifndef _MEM_CHUNK_H_
#define _MEM_CHUNK_H_

#include <cstdint>
#include <list>
#include "utils.h"
#include "MemUsageLogger.h"

extern MemUsageLogger *memLogger;
const uint32_t DEF_BLOCK_SIZE=20480;

class MemChunk {
public:
    MemChunk(uint32_t _dbsize = DEF_BLOCK_SIZE): allocations(0), dynamic_block_size(_dbsize), number_of_blocks(0), free_index(0) {}
    ~MemChunk(){}
    char* get_address(size_t size);
    void delete_dynamic_blocks();
    void increment_allocations() { allocations++; };
    void decrement_allocations() { if(--allocations == 0) delete_dynamic_blocks(); };
private:
    long allocations;
    typedef std::list<std::pair<uint8_t*, int>> BlockList;
    BlockList dynamic_block_list;
    const uint32_t dynamic_block_size;
    uint32_t number_of_blocks;
    uint32_t free_index;
};

inline char* MemChunk::get_address(size_t size){
    uint32_t mask = L2_CACHE_LINE-1; // Align on a cache line
    uint32_t index_increment = (size & ~mask) + (!!(size & mask) << 6); // 2^6 = 64
    if((number_of_blocks == 0) || (free_index + index_increment >= dynamic_block_size)){
        void *ptr = NULL;
        int alloc_size = index_increment > dynamic_block_size ? index_increment : dynamic_block_size;
        int mflag = posix_memalign(&ptr, L2_CACHE_LINE, alloc_size * sizeof(uint8_t));
        if (memLogger) memLogger->addTmpMem(alloc_size * sizeof(uint8_t));
        dynamic_block_list.push_back(std::make_pair((uint8_t *) ptr, alloc_size));
        number_of_blocks++;
        free_index = 0;
    }
    uint8_t* pointer = dynamic_block_list.back().first + free_index;
    free_index += index_increment;
    return (char*)pointer;
}

inline void MemChunk::delete_dynamic_blocks(){
    for(auto pair : dynamic_block_list){
        if(memLogger) memLogger->delTmpMem(pair.second * sizeof(uint8_t));
        free(pair.first);
    }
    dynamic_block_list.clear(); number_of_blocks = 0; free_index = 0;
}
#endif//_MEM_CHUNK_H_
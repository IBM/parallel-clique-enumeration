#ifndef _GRAPH_UTILS_H_
#define _GRAPH_UTILS_H_

#include <cstdint>
#include <sstream>
#include <fstream>
#include <tbb/combinable.h>
#include <tbb/spin_mutex.h>
#include <tbb/atomic.h>
#include <immintrin.h>
#include <sys/mman.h>
#include <unistd.h>
#include <iostream>
#include <algorithm>

/** defines whether to use AVX512 instruction set **/
#define USE_AVX512
/**** For SimpleHashSet ****/
/** defines whether to use vector lookup with gather instructions **/
#define USE_VECTOR_LOOKUP
/** defines whether to use hopscotch hashing **/
#define USE_HOPSCOTCH

#ifndef __AVX512F__
#undef USE_AVX512
#endif

typedef int32_t KeyType;

const size_t L2_CACHE_LINE = 64;
const size_t HS_BUCKET_SIZE = 1*L2_CACHE_LINE/sizeof(int32_t);

const KeyType EMPTY_KEY = 0xFFFFFFFF;
const KeyType INVAL_KEY = 0xEEEEEEEE;
const int8_t EMPTY_BYTE = 0xFF;
const int8_t INVAL_BYTE = 0xEE;

#ifdef USE_AVX512
const size_t VECTOR_SIZE = 16;
#else
const size_t VECTOR_SIZE = 1;
#endif

#ifdef USE_AVX512
const __m512i empty_vec = _mm512_set1_epi32(EMPTY_KEY);
const __m512i zero_vec = _mm512_set1_epi32(0);
const __m512i one_vec = _mm512_set1_epi32(1);
const __m512i inval_vec = _mm512_set1_epi32(INVAL_KEY);
#endif

inline char* getCmdOption(char ** begin, char ** end, const std::string & option) {
    char ** itr = std::find(begin, end, option);
    if (itr != end && ++itr != end) return *itr;
    return 0;
}

inline bool cmdOptionExists(char** begin, char** end, const std::string& option) {
    return std::find(begin, end, option) != end;
}

inline void printHelp()
{
    std::cout << " Maximal clique enumeration algorithm " << std::endl;
    std::cout << "    -f                Path to the input file graph" << std::endl;
    std::cout << "    -p                Prints the resulting clique histogram, no argument" << std::endl;
    std::cout << "    -n                Number of threads, default 256" << std::endl;
    std::cout << "    -o                Turns on memory allocation grouping, no argument" << std::endl;
    std::cout << "    -b                Memory block size in bytes used for memory allocation grouping, default 20 MB" << std::endl;
    std::cout << "    --thresh          Threshold tt for P+X for task grouping, default 30" << std::endl;
    std::cout << "    --mem-thresh      Threshold tm for P+X for memory allocation grouping, default 20" << std::endl;
    std::cout << "    --max-clq         Size of the maximum clique to be explored" << std::endl;
    std::cout << "    -s,               Defines subgraph based approach for BK algorithm," << std::endl;
    std::cout << "                          0 - don't create subgraphs" << std::endl;
    std::cout << "                          1 - create subgraphs in the outer level of the algorithm" << std::endl;
    std::cout << "                          2 - create subgraphs per task" << std::endl;
    std::cout << "    --ord             Defines which ordering is used, default degeneracy" << std::endl;
    std::cout << "                          0 - degeneracy ordering" << std::endl;
    std::cout << "                          1 - degree ordering" << std::endl;
    std::cout << "                          2 - inverse degree ordering" << std::endl;
    std::cout << "    -m                Turns memory profiling on and defines path to the output csv file" << std::endl;
    std::cout << "    -i                Defines sampling interval for memory profiling, default 10000" << std::endl;
    std::cout << "    -h, --help        Shows this message" << std::endl;
}

#endif//_GRAPH_UTILS_H_

#ifndef _SIMPLE_HASH_TABLE_H_
#define _SIMPLE_HASH_TABLE_H_

#include <cstdint>
#include <immintrin.h>
#include <vector>

#include "MemChunk.h"
#include "utils.h"

extern MemUsageLogger *memLogger;

class SimpleHashTable {
public:
    SimpleHashTable(MemChunk* _chunk = NULL, int mtype = MemType::OTHER) :
        array(NULL), capacity(0), phys_capacity(0), num_of_elems(0),
        mem_type(mtype), number_of_attempts(0), my_chunk(_chunk)
    {
        if(memLogger && !my_chunk) memLogger->addTmpMem(sizeof(SimpleHashTable), mem_type);
    }
    ~SimpleHashTable(){
        if(my_chunk == NULL) {
            if(memLogger)memLogger->delTmpMem(phys_capacity * sizeof(KeyType) + sizeof(SimpleHashTable), mem_type);
            delete[] array;
        }
    }

    void insert(KeyType el);
#ifdef USE_HOPSCOTCH
    void hopscotchInsert(KeyType el);
#endif

    size_t size() { return num_of_elems; };
    void reserve(size_t cap);
    KeyType hashFunction(KeyType el);
    __m512i vectorHashFunction(__m512i el_vector);
    bool contains(KeyType el, int pos = -1);
#ifdef USE_AVX512
    __mmask16 vector_lookup(__m512i el_vector, __mmask16 mask = 0xFFFF);
#endif

    template <typename TF>
    void for_each(TF&& f);

    int mem_type;
private:
    uint32_t a_hash, b_hash, M_hash; // hash function params
#ifdef USE_HOPSCOTCH
    bool hopscotchTryInsert(KeyType el);
    void hopscotchReconstruct();
#endif
    KeyType* array;
    size_t capacity, phys_capacity;
    size_t num_of_elems, number_of_attempts;
    MemChunk* my_chunk;
    friend class SimpleHashSet;
};

inline KeyType SimpleHashTable::hashFunction(KeyType el) {
    uint32_t h = (uint32_t) ((uint32_t)el * a_hash + b_hash);
    return h >> (32 - M_hash);
}

inline __m512i SimpleHashTable::vectorHashFunction(__m512i el_vector){
    __m512i a_vec = _mm512_set1_epi32(a_hash);
    __m512i b_vec = _mm512_set1_epi32(b_hash);
    __m512i rem_vec = _mm512_mullo_epi32(a_vec, el_vector);
    rem_vec = _mm512_add_epi32(rem_vec, b_vec);
    rem_vec = _mm512_srli_epi32 (rem_vec, 32 - M_hash);
    return rem_vec;
}

template <typename TF>
inline void SimpleHashTable::for_each(TF&& f) {
    for(size_t i = 0; i < phys_capacity; i++) if(array[i] != EMPTY_KEY && array[i] != INVAL_KEY) f(array[i]);
}

inline void SimpleHashTable::reserve(size_t cap) {
    capacity = (1 << (32 - __builtin_clz(2*cap-1)));
    num_of_elems = 0;
    phys_capacity = capacity + VECTOR_SIZE - 1;
    #ifndef USE_HOPSCOTCH
    number_of_attempts = phys_capacity;
    #else
    number_of_attempts = HS_BUCKET_SIZE;
    #endif
    M_hash = 32 - __builtin_clz(2*cap-1); uint32_t local_seed = time(NULL);
    a_hash = rand_r(&local_seed) | 1; b_hash = rand_r(&local_seed) & ((1 << (32 - M_hash))-1);

    if(my_chunk == NULL) {
        array = new KeyType [phys_capacity];
        if(memLogger) memLogger->addTmpMem(phys_capacity * sizeof(KeyType), mem_type);
    }
    else array = new( my_chunk->get_address(phys_capacity * sizeof(KeyType)) ) KeyType [phys_capacity];
    memset(array, EMPTY_BYTE, sizeof(KeyType)*capacity);
    memset(array+capacity, INVAL_BYTE, sizeof(KeyType)*(phys_capacity-capacity));
}

#ifndef USE_AVX512
inline bool SimpleHashTable::contains(KeyType el, int pos) {
    if(!size()) return false;
    pos = pos == -1 ? hashFunction(el) : pos;
    int found = false;
    for(size_t i = 0; i < number_of_attempts; i++) {
        if(array[pos] == el) {
            found = true; break;
        }
        if(array[pos] == -1) break;
        pos = (pos + 1) % capacity;
    }
    return found;
}
#else
inline bool SimpleHashTable::contains(KeyType el, int pos) {
    if(!size()) return false;
    pos = pos == -1 ? hashFunction(el) : pos;
    __m512i el_vec = _mm512_set1_epi32(el);
    bool found = false;
    for(size_t i = 0; i < number_of_attempts/VECTOR_SIZE + 1; i++) {
        __m512i elem_vec = _mm512_maskz_loadu_epi32(0xFFFF,(void*)(array + pos));
        __mmask16 el_cmp_mask = _mm512_cmpeq_epi32_mask(elem_vec, el_vec);
        __mmask16 empty_cmp_mask = _mm512_cmpeq_epi32_mask(elem_vec, empty_vec);
        if(el_cmp_mask) {
            found = true; break;
        }
        if(empty_cmp_mask) break;
        pos = pos + VECTOR_SIZE;
        if(pos > capacity) pos = 0;
    }
    return found;
}

inline __mmask16 SimpleHashTable::vector_lookup(__m512i el_vector, __mmask16 mask) {
    if(!size()) return 0;
    __m512i rem_vec = vectorHashFunction(el_vector);
    __m512i fetched_vec = _mm512_mask_i32gather_epi32(inval_vec, mask, rem_vec, array, /*scale*/ 4);
    __mmask16 el_cmp_mask = _mm512_mask_cmpeq_epi32_mask(mask, el_vector, fetched_vec);
    __mmask16 empty_cmp_mask = _mm512_cmpeq_epi32_mask(empty_vec, fetched_vec);
    __mmask16 done_mask = el_cmp_mask | empty_cmp_mask | (~mask);
    for(int i = 0; i < VECTOR_SIZE; i++) {
        __mmask16 tmp_mask = 1 << i;
        if((~done_mask) & tmp_mask) {
            bool found = contains( ((KeyType*)(&el_vector))[i], ((int*)(&rem_vec))[i] );
            el_cmp_mask |= found << i;
        }
    }
    return el_cmp_mask;
}
#endif

#ifdef USE_HOPSCOTCH
inline int distance(int first, int second, int cap){
    return first >= second ? first - second : first - second + cap;
}

inline void SimpleHashTable::hopscotchInsert(KeyType el) {
    const int nAttempts = 10; bool success = false;
    for(int cnt = 0; cnt < nAttempts; cnt++) if(success = hopscotchTryInsert(el)) break; else hopscotchReconstruct();
    if(!success) {std::cout << "Unsuccessful hopscotch insert" << std::endl; exit(1);}
}

inline bool SimpleHashTable::hopscotchTryInsert(KeyType el) {
    int pos = hashFunction(el);
    int init_pos = pos;

    // Find an empty space or the element
    for(size_t i = 0; i < capacity; i++) {
        if(array[pos] == EMPTY_KEY) break;
        if(array[pos] == el) return true;
        pos = (pos + 1) % capacity;
    }

    // Move the empty space to the neighborhood of the initial position
    while(distance(pos, init_pos, capacity) >= HS_BUCKET_SIZE) {
        bool found = false;
        int start = pos - HS_BUCKET_SIZE+1;
        if(start < 0) start += capacity;
        for(int j = start; j != pos; j = (j+1) % capacity){
            if(distance(pos,hashFunction(array[j]), capacity) <= HS_BUCKET_SIZE - 1){
                array[pos] = array[j]; pos = j; found = true; break;
            }
        }
        if(!found) {
            hopscotchReconstruct(); return false;
        }
    }
    array[pos] = el;
    num_of_elems++;
    return true;
}

inline void SimpleHashTable::hopscotchReconstruct() {
    KeyType* oldArray = array;
    int old_capac = phys_capacity;
    reserve(capacity);
    for(size_t i = 0; i < old_capac; i++) {
        if (oldArray[i] != EMPTY_KEY && oldArray[i] != INVAL_KEY) hopscotchInsert(oldArray[i]);
    }
    delete [] oldArray;
    if(memLogger && !my_chunk) memLogger->delTmpMem(phys_capacity * sizeof(KeyType), mem_type);
}
#endif

#ifndef USE_AVX512
inline void SimpleHashTable::insert(KeyType el) {
    int pos = hashFunction(el);
    for(size_t i = 0; i < number_of_attempts; i++) {
        if(array[pos] == -1) break;
        if(array[pos] == el) return;
        pos = (pos + 1) % capacity;
    }
    array[pos] = el;
    num_of_elems++;
}
#else
inline void SimpleHashTable::insert(KeyType el) {
    int pos = hashFunction(el);
    __m512i el_vec = _mm512_set1_epi32(el);
    int result_pos = -1;
    for(size_t i = 0; i < number_of_attempts/VECTOR_SIZE + 1; i++) {
        __m512i elem_vec = _mm512_maskz_loadu_epi32(0xFFFF,(void*)(array + pos));
        __mmask16 el_cmp_mask = _mm512_cmpeq_epi32_mask(elem_vec, el_vec);
        __mmask16 empty_cmp_mask = _mm512_cmpeq_epi32_mask(elem_vec, empty_vec);
        int offset = __builtin_ctz(empty_cmp_mask);
        if(el_cmp_mask) return; // Element is already in the table
        if(empty_cmp_mask != 0) {
            result_pos = pos + offset; break;
        }
        pos = pos + VECTOR_SIZE;
        if(pos > capacity) pos = 0;
    }
    array[result_pos] = el;
    num_of_elems++;
}
#endif

#endif//_SIMPLE_HASH_TABLE_H_

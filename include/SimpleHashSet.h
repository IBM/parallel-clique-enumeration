#ifndef _SIMPLE_HASH_SET_H
#define _SIMPLE_HASH_SET_H

#include <vector>
#include <algorithm>
#include <unordered_set>

#include "UnrolledList.h"
#include "SimpleHashTable.h"
#include "MemChunk.h"
#include "utils.h"

extern MemUsageLogger *memLogger;

class SimpleHashSet {
public:
    /*********** Constructors & destructors *************/
    SimpleHashSet(MemChunk* _chunk = NULL, int mtype = MemType::OTHER) :
            my_chunk(_chunk), mem_type(mtype), hashed_elems(_chunk, mtype),
            elems(_chunk, mtype), isHashed(false) {};

    SimpleHashSet(SimpleHashSet& s) : my_chunk(NULL), mem_type(s.mem_type), elems(s.elems) {};
    SimpleHashSet* clone() { return(new SimpleHashSet(*this)); }
    ~SimpleHashSet() { if(memLogger) memLogger->delTmpMem(sizeof(SimpleHashSet), mem_type); };
    /*********** Interface methods *************/
    SimpleHashSet* intersect(SimpleHashSet *other, MemChunk* chunk = NULL, int type = MemType::OTHER); // Intersection
    SimpleHashSet* include(SimpleHashSet *other, MemChunk* chunk = NULL, int type = MemType::OTHER); // Union
    SimpleHashSet* exclude(SimpleHashSet *other, MemChunk* chunk = NULL, int type = MemType::OTHER); // Difference
    int intersection_size(SimpleHashSet *other);

    void hashSet();
    void add_elem(KeyType el) { if(isHashed) hashed_elems.insert(el); else elems.push_back(el); }
    void del_elem(KeyType el) { if(!isHashed) elems.remove(el); }
    bool contains(KeyType el) { if(isHashed) return hashed_elems.contains(el); else return false; }
    int size() { if(isHashed) return hashed_elems.size(); else return elems.size(); };
    bool empty() { return size() == 0; };
    KeyType get_first() {  if(empty() || isHashed) return -1; else return elems.front(); };
    KeyType get_last() { if(elems.empty() || isHashed) return -1; else return elems.back(); };

    template <typename TF>
    void for_each(TF&& f);

    // Iterators
    int get_next() { if(!isHashed) return elems.get_next(); else return -1; };
    void reset_iterator() { if(!isHashed) elems.reset_iterator(); };
    bool end_iter() { if(!isHashed) return elems.end_iter(); else return false; };

    void set_mem_chunk(MemChunk* chunk) { my_chunk = chunk; elems.set_mem_chunk(my_chunk); };
    static SimpleHashSet* create_set(MemChunk *chunk = NULL, int mem_type = MemType::OTHER);

    int mem_type;
    MemChunk* my_chunk;
private:
    void reserve(size_t size) { if(isHashed) hashed_elems.reserve(size); };

    bool isHashed;
    UnrolledList elems;
    SimpleHashTable hashed_elems;
};

inline void SimpleHashSet::hashSet(){
    if(isHashed) return;
    size_t size = elems.size();
    hashed_elems.reserve(size);
    for(size_t i = 0; i < size; i++) {
#ifdef USE_HOPSCOTCH
        hashed_elems.hopscotchInsert(elems.back());
#else
        hashed_elems.insert(elems.back());
#endif
        elems.pop_back();
    }
    isHashed = true;
}

template <typename TF>
inline void SimpleHashSet::for_each(TF&& f) {
    if(isHashed) hashed_elems.for_each([&](int el){ f(el); });
    else elems.for_each([&](int el){ f(el); });
}

inline SimpleHashSet* SimpleHashSet::create_set(MemChunk *chunk, int mem_type) {
    SimpleHashSet* novi = NULL;
    if(chunk == NULL) {
        novi = new SimpleHashSet(NULL, mem_type);
        if(memLogger) memLogger->addTmpMem(sizeof(SimpleHashSet), mem_type);
    }
    else novi = new( chunk->get_address(sizeof(SimpleHashSet)) ) SimpleHashSet(chunk);
    return novi;
}

inline SimpleHashSet* SimpleHashSet::exclude(SimpleHashSet *other, MemChunk* chunk, int type) {
    if(!other) return NULL;
    auto *newSet = create_set(chunk, type);
    this->for_each([&](int node) { if(!other->contains(node)) newSet->elems.push_back(node); });
    return newSet;
}

inline SimpleHashSet* SimpleHashSet::include(SimpleHashSet *other, MemChunk* chunk, int type) {
    if(!other) return NULL;
    auto *newSet = create_set(chunk, type);
    this->for_each([&](int node)  { newSet->elems.push_back(node); });
    other->for_each([&](int node) { newSet->elems.push_back(node); });
    return newSet;
}

#if !defined(USE_VECTOR_LOOKUP) || !defined(USE_AVX512)
inline SimpleHashSet* SimpleHashSet::intersect(SimpleHashSet *other, MemChunk* chunk, int type) {
    if(!other) return NULL;
    auto *newSet = create_set(chunk, type);
    this->for_each([&](int node) { if(other->contains(node)) newSet->elems.push_back(node); });
    return newSet;
}
#else
inline SimpleHashSet* SimpleHashSet::intersect(SimpleHashSet *other, MemChunk* chunk, int type) {
    if(!other) return NULL;
    auto *newSet = create_set(chunk, type);
    UnrolledList::Bucket *pom = elems.head;
    int pom_offs = 0;
    const __mmask16 full_mask = 0xFFFF;
    const __mmask16 end_mask = END_MASK; // the last two elements are a pointer
    __mmask16 list_end_mask = (1 << (elems.offset + 1)) - 1;
    while(pom) {
        __mmask16 tmp_end_mask = pom == elems.tail ? list_end_mask : end_mask;
        pom_offs = 0;
        for(; pom_offs < ELEMS_IN_BUCKET; pom_offs += VECTOR_SIZE) {
            __m512i load_vec = _mm512_load_epi32(pom->elems + pom_offs);
            __mmask16 this_end_mask = pom_offs + VECTOR_SIZE >= ELEMS_IN_BUCKET ? tmp_end_mask : full_mask;
            __mmask16 found_mask = other->hashed_elems.vector_lookup(load_vec, this_end_mask);
           for(int i = 0; i < VECTOR_SIZE; i++) {
                if(found_mask & (1 << i) ) newSet->elems.push_back(((KeyType*)(&load_vec))[i]);
            }
        }
        pom = pom->next;
    }
    return newSet;
}
#endif

#if !defined(USE_VECTOR_LOOKUP) || !defined(USE_AVX512)
inline int SimpleHashSet::intersection_size(SimpleHashSet *other) {
    if(!other) return 0;
    int size = 0;
    this->for_each([&](int node) { if(other->contains(node)) size++; });
    return size;
}
#else
inline int SimpleHashSet::intersection_size(SimpleHashSet *other) {
    if(!other) return 0;
    int size = 0, pom_offs = 0;
    UnrolledList::Bucket *pom = elems.head;
    const __mmask16 full_mask = 0xFFFF;
    const __mmask16 end_mask = END_MASK; // the last two elements are a pointer
    __mmask16 list_end_mask = (1 << (elems.offset + 1)) - 1;
    while(pom) {
        __mmask16 tmp_end_mask = pom == elems.tail ? list_end_mask : end_mask;
        pom_offs = 0;
        for(; pom_offs < ELEMS_IN_BUCKET; pom_offs += VECTOR_SIZE) {
            __m512i load_vec = _mm512_load_epi32(pom->elems + pom_offs);
            __mmask16 this_end_mask = pom_offs + VECTOR_SIZE >= ELEMS_IN_BUCKET ? tmp_end_mask : full_mask;
            __mmask16 found_mask = other->hashed_elems.vector_lookup(load_vec, this_end_mask);
            size += _mm_popcnt_u32(found_mask);
        }
        pom = pom->next;
    }
    return size;
}
#endif
#endif //_SIMPLE_HASH_SET_H
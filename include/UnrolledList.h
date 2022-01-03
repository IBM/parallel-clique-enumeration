#ifndef _UNROLLED_LIST_H_
#define _UNROLLED_LIST_H_

#include <cassert>
#include <utility>
#include <immintrin.h>
#include <string>
#include <iostream>

#include "MemChunk.h"
#include "utils.h"

using namespace std;
extern MemUsageLogger *memLogger;

constexpr size_t BUCKET_SIZE = L2_CACHE_LINE;
constexpr size_t ELEMS_IN_BUCKET = (BUCKET_SIZE)/sizeof(KeyType);
const __mmask16 END_MASK = 0xFFFF;

class UnrolledList {
public:
    /*********** Constructors & destructors *************/
    UnrolledList(MemChunk *chunk = NULL, int type = MemType::OTHER);
    UnrolledList(UnrolledList& other, int type = MemType::OTHER);
    ~UnrolledList();

    /*********** public methods *************/
    void push_back(KeyType el);
    void remove(KeyType el);
    void pop_back();

    template <typename TF>
    void for_each(TF&& f);

    size_t size() {  return num_of_elems; };
    bool empty() { return size() == 0; };
    KeyType front() { if(empty()) return EMPTY_KEY; else return head->elems[0]; }
    KeyType back() { if(empty()) return EMPTY_KEY; else return tail->elems[offset]; };
    bool IsSorted() { return isSorted; };

    // Iterators
    int get_next();
    void reset_iterator() {  iter = NULL; iter_offs = 0; };
    bool end_iter() { if(!head || (iter == tail && iter_offs == offset)) return true; else return false; };

    void set_mem_chunk(MemChunk* chunk) {  my_chunk = chunk; };
    static UnrolledList* create_list(MemChunk *chunk = NULL, int type = MemType::OTHER);

    int mem_type;
private:
    friend class SimpleHashSet;
    /*********** struct definition *************/
    struct Bucket {
        Bucket() : next(NULL) { memset(elems, INVAL_BYTE, sizeof(KeyType)*ELEMS_IN_BUCKET); }
        void* operator new(size_t size) {
            void *ptr = NULL;
            int mflag = posix_memalign(&ptr, L2_CACHE_LINE, size);
            return ptr;
        }
        void* operator new(size_t size, char* memptr) { return memptr; };
        void operator delete(void* ptr) { free(ptr); ptr = NULL; return; }
        void operator delete(void* ptr, char* memptr) {};

        KeyType elems[ELEMS_IN_BUCKET];
        struct Bucket *next;
    };
    /*********** private fields *************/
    Bucket *head, *tail;  // pointers to the head and tail of the list
    int offset; // Offsets of the last element the tail bucket
    size_t num_of_buckets; // Number of list elements
    volatile size_t num_of_elems;
    MemChunk* my_chunk; // Memory pool references
    bool isSorted; // is the list sorted, false if push_back is called
    // Iterator for get_next
    Bucket *iter;
    int iter_offs;
    /*********** private methods  *************/
    std::pair<Bucket*, Bucket*> copy_list(MemChunk *chunk = NULL);
    std::pair<Bucket*, int> find_element(KeyType el);
    void kill_list();
    static Bucket* create_bucket(MemChunk *chunk = NULL, int type = MemType::OTHER);
    void delete_bucket(Bucket *le);
};

/*********** Constructors & destructors *************/

inline UnrolledList::UnrolledList(MemChunk *chunk, int mtype) :
    head(NULL), tail(NULL), offset(0), num_of_buckets(0),
    num_of_elems(0), iter(NULL), iter_offs(0),
    mem_type(mtype), my_chunk(chunk), isSorted(true)
{
    if(memLogger && !my_chunk) {
        memLogger->addTmpMem(sizeof(UnrolledList), mem_type);
    }
}

inline UnrolledList::UnrolledList(UnrolledList& other, int type) :
        head(NULL), tail(NULL), offset(other.offset), num_of_buckets(other.num_of_buckets),
        num_of_elems(other.num_of_elems), iter(NULL), iter_offs(0),
        mem_type(type != MemType::OTHER ? type : other.mem_type),
        my_chunk(other.my_chunk), isSorted(other.isSorted)
{
    auto new_pair = copy_list(my_chunk);
    head = new_pair.first;
    tail = new_pair.second;
    if(memLogger && !my_chunk) memLogger->addTmpMem(sizeof(UnrolledList), mem_type);
}

inline UnrolledList::~UnrolledList() {
    if(memLogger && !my_chunk) { memLogger->delTmpMem(sizeof(UnrolledList), mem_type); }
    kill_list();
};

template <typename TF>
inline void UnrolledList::for_each(TF&& f) {
    if(!head || empty()) return;
    UnrolledList::Bucket *pom = head;
    int pom_offs = 0;
    while(pom && !(pom == tail && pom_offs == offset + 1)) {
        f(pom->elems[pom_offs]);
        pom_offs++;
        if(ELEMS_IN_BUCKET == pom_offs) {
            pom_offs = 0; pom = pom->next;
        }
    }
}

inline int UnrolledList::get_next() {
    if(!head || (iter == tail && iter_offs == offset)) return -1;
    if(!iter) {
        iter = head; iter_offs = 0;
    }
    else {
        iter_offs++;
        if(ELEMS_IN_BUCKET == iter_offs) {
            iter_offs = 0; iter = iter->next;
        }
    }
    return iter->elems[iter_offs];
}

inline UnrolledList* UnrolledList::create_list(MemChunk *chunk, int type)
{
    UnrolledList* novi = NULL;
    if(chunk == NULL) novi = new UnrolledList(NULL, type);
    else novi = new( chunk->get_address(sizeof(UnrolledList)) ) UnrolledList(chunk);
    return novi;
}

inline UnrolledList::Bucket* UnrolledList::create_bucket(MemChunk *chunk, int type) {
    UnrolledList::Bucket* novi = NULL;
    if(chunk == NULL) {
        novi = new UnrolledList::Bucket;
        if(CollectMemUsage) memLogger->addTmpMem(sizeof(UnrolledList::Bucket), type);
    }
    else novi = new( chunk->get_address(sizeof(UnrolledList::Bucket)) ) UnrolledList::Bucket;
    return novi;
}

inline void UnrolledList::delete_bucket(UnrolledList::Bucket *le) {
    if(!my_chunk) {
        delete le;
        if(CollectMemUsage) memLogger->delTmpMem(sizeof(UnrolledList::Bucket), mem_type);
    }
    else le->~Bucket();
}
#endif//_UNROLLED_LIST_H_
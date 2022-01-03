#include "UnrolledList.h"
#include <immintrin.h>

void UnrolledList::push_back(KeyType el) {
    if(head == NULL) {
        head = create_bucket(my_chunk, mem_type);
        tail = head; num_of_buckets++; offset = 0;
        tail->elems[offset] = el; num_of_elems++;
        return;
    }
    int prev_elem = tail->elems[offset];
    if(offset == ELEMS_IN_BUCKET-1) {
        tail->next = create_bucket(my_chunk, mem_type);
        tail = tail->next; num_of_buckets++; offset = 0;
    }
    else offset++;
    tail->elems[offset] = el; num_of_elems++;
    if(prev_elem > el) isSorted = false;
}

// Assumes there is at most one el key
void UnrolledList::remove(KeyType el) {
    auto location = find_element(el);
    if(location.second == -1) return;
    location.first->elems[location.second] = tail->elems[offset];
    pop_back();
}

void UnrolledList::pop_back() {
    num_of_elems--;
    if(offset == 0){
        if(head == tail){
            delete_bucket(head); head = NULL; tail = NULL; offset = 0;
        }
        else {
            Bucket* tmp = head;
            while(tmp->next != tail) tmp = tmp->next;
            delete_bucket(tmp->next);
            tmp->next = NULL; tail = tmp; offset = ELEMS_IN_BUCKET - 1;
        }
        num_of_buckets--;
    }
    else offset--;
}

/*********** private methods  *************/

std::pair<UnrolledList::Bucket*, int> UnrolledList::find_element(KeyType el) {
    Bucket *pom = head;
    int pom_offs = 0;
    bool found = false;

#ifdef USE_AVX512
    const __mmask16 end_mask = END_MASK; // the last two elements are a pointer
    __m512i el_vec = _mm512_set1_epi32(el);
    while(pom){
        pom_offs = 0;
        for(; pom_offs < ELEMS_IN_BUCKET; pom_offs += VECTOR_SIZE) {
            __m512i load_vec = _mm512_maskz_loadu_epi32(0xFFFF, pom->elems + pom_offs);
            __mmask16 cmp_mask = _mm512_cmpeq_epi32_mask(el_vec, load_vec);
            cmp_mask = pom_offs + VECTOR_SIZE >= ELEMS_IN_BUCKET ? cmp_mask & end_mask : cmp_mask;
            if(cmp_mask) {
                pom_offs += __builtin_ctz(cmp_mask); found = true;break;
            }
        }
        if(found) break;
        pom = pom->next;
    }
#else
    while(pom && !(pom == tail && pom_offs == offset + 1)) {
        if (pom->elems[pom_offs] == el) {
            found = true; break;
        }
        pom_offs++;
        if (ELEMS_IN_BUCKET == pom_offs) {
            pom_offs = 0; pom = pom->next;
        }
    }
#endif
    if(found) return std::make_pair(pom, pom_offs);
    else return std::make_pair((Bucket*) NULL, -1);
}

void UnrolledList::kill_list(){
    while(NULL != head){
        UnrolledList::Bucket *pom = head; head = head->next; delete_bucket(pom);
    }
    head = NULL; tail = NULL;
    offset = 0; num_of_elems = 0; num_of_buckets = 0;
}

std::pair<UnrolledList::Bucket*, UnrolledList::Bucket*> UnrolledList::copy_list(MemChunk *chunk) {
    Bucket *new_head = NULL, *new_tail = NULL, *pom = head;
    while(NULL != pom) {
        Bucket *novi = create_bucket(chunk, mem_type);
        if(!new_head) new_head = novi;
        else new_tail->next = novi;
        new_tail = novi;
        for(int i = 0; i < ELEMS_IN_BUCKET; i++) novi->elems[i] = pom->elems[i];
        pom = pom->next;
    }
    return std::make_pair(new_head, new_tail);
}

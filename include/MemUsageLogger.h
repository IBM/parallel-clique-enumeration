#ifndef _MEM_USAGE_LOGGER_H_
#define _MEM_USAGE_LOGGER_H_

#include <ctime>
#include <cstdlib>
#include <iostream>
#include <fstream>
#include <vector>

#include <tbb/tick_count.h>
#include <tbb/combinable.h>
#include <tbb/spin_mutex.h>
#include <tbb/atomic.h>

using namespace std;

typedef tbb::spin_mutex MemUsageMutexType;
extern bool CollectMemUsage;

enum MemType { SET = 0, SUBGRAPH, GRAPH, CLIQUE, TASK, ROOT_SET, ROOT_SUBGRAPH,
        ROOT_CLIQUE, ROOT_TASK, OTHER, NUM_OF_TYPES };
const std::string TYPE_NAMES[] = {"Sets", "Subgraphs", "Graph", "Cliques", "Tasks",
                                  "RootSets","RootSubgraphs", "RootCliques", "RootTasks", "Other"};

class MemUsageLogger {
public:
    MemUsageLogger(string out_file_name, long _si = 10000) :
        tmp_type_usage_pt(MemType::NUM_OF_TYPES), peak_type_usage(MemType::NUM_OF_TYPES),
        tmp_combine_semaphore(false), peak_usage(0),
        t_start(tbb::tick_count::now()), sampling_interval(_si),
        mem_log_stream(out_file_name)
    {
        mem_log_stream << "Timestamp, Total";
        for(int i = 0; i < MemType::NUM_OF_TYPES; i++)
            mem_log_stream << ", " << TYPE_NAMES[i];
        mem_log_stream << endl;
    }

    ~MemUsageLogger() { }

    void addTmpMem(long mem, int type = MemType::OTHER);
    void delTmpMem(long mem, int type = MemType::OTHER);
    void printData();
private:
    void add_memory_usage(long mem, tbb::combinable<long>& pt_usage);
    void del_memory_usage(long mem, tbb::combinable<long>& pt_usage);

    void update_peak_usage();
    long combine_mem_usage_data(tbb::combinable<long>& pt_usage);

    MemUsageMutexType MemUsageMutex;
    tbb::combinable<long> total_usage_pt, tmp_usage_pt, allocation_num_pt;
    long peak_usage;

    std::vector<tbb::combinable<long>> tmp_type_usage_pt;
    std::vector<long> peak_type_usage;
    tbb::atomic<bool> tmp_combine_semaphore;
    tbb::combinable<unsigned int> pt_seed;

    tbb::tick_count t_start;
    long sampling_interval;
    ofstream mem_log_stream;
};

inline void MemUsageLogger::addTmpMem(long mem, int type) {
    while(tmp_combine_semaphore);
    add_memory_usage(mem, tmp_usage_pt);
    add_memory_usage(mem, tmp_type_usage_pt[type]);
    update_peak_usage();
    bool exists = false;
    auto& local_cnt = allocation_num_pt.local(exists);
    if(!exists) local_cnt = 0;
    local_cnt++;
}

inline void MemUsageLogger::delTmpMem(long mem, int type) {
    while(tmp_combine_semaphore);
    del_memory_usage(mem, tmp_usage_pt);
    del_memory_usage(mem, tmp_type_usage_pt[type]);
    update_peak_usage();
}

inline void MemUsageLogger::update_peak_usage() {
    bool exists = false;
    auto& local_seed = pt_seed.local(exists);
    if(!exists) local_seed = time(NULL);

    // Negative sampling interval blocks printing and enables only counting the number of allocations
    if((sampling_interval > 0) && (rand_r(&local_seed) % sampling_interval == sampling_interval-1) ) {
        MemUsageMutexType::scoped_lock lock(MemUsageMutex);
        while (tmp_combine_semaphore);
        tmp_combine_semaphore = true;

        long tmp_usage = combine_mem_usage_data(tmp_usage_pt);

        if (tmp_usage > peak_usage) {
            peak_usage = tmp_usage;
            for(int i = 0; i < MemType::NUM_OF_TYPES; i++)
                peak_type_usage[i] = combine_mem_usage_data(tmp_type_usage_pt[i]);
        }

        auto tstamp = tbb::tick_count::now();

        mem_log_stream << (tstamp - t_start).seconds() << ", " << tmp_usage/1024.0;
        for(int i = 0; i < MemType::NUM_OF_TYPES; i++)
            mem_log_stream << ", " << combine_mem_usage_data(tmp_type_usage_pt[i])/1024.0;
        mem_log_stream << endl;

        tmp_combine_semaphore = false;
    }
}

inline void MemUsageLogger::add_memory_usage(long mem, tbb::combinable<long>& pt_usage) {
    bool exists = false;
    auto& local_usage = pt_usage.local(exists);
    if(!exists) local_usage = 0;
    local_usage += mem;
}

inline void MemUsageLogger::del_memory_usage(long mem, tbb::combinable<long>& pt_usage) {
    bool exists = false;
    auto& local_usage = pt_usage.local(exists);
    if(!exists) local_usage = 0;
    local_usage -= mem;
}

inline long MemUsageLogger::combine_mem_usage_data(tbb::combinable<long>& pt_usage) {
    long total_mem_usage = 0;
    pt_usage.combine_each([&](long sum) { total_mem_usage += sum; });
    return total_mem_usage;
}

inline void MemUsageLogger::printData(){
    long total_usage =combine_mem_usage_data(total_usage_pt);
    long allocation_num =combine_mem_usage_data(allocation_num_pt);
    cout << "Peak memory usage: " << peak_usage/1024 << " KB" << endl;
    for(int i = 0; i < MemType::NUM_OF_TYPES; i++)
        cout << "   "  << TYPE_NAMES[i] << " :       " << peak_type_usage[i]/1024 << " KB" << endl;
    cout << "Number of allocations: " << allocation_num << endl;
}
#endif
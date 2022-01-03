#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>
#include "BKTask.h"

using namespace std;
using namespace tbb;

extern int subgraphBased;
extern bool setsMempool;
extern bool noEndPrint;

void GraphGuard::inc_graph_ref_count() {
    RefCountMutexType::scoped_lock lock(RefCountMutex);
    ref_count++;
}

void GraphGuard::dec_graph_ref_count(GraphGuard*& this_guard) {
    RefCountMutexType::scoped_lock lock(RefCountMutex);
    ref_count--;
    if(ref_count <= 0) {
        if(!graph->my_chunk) delete graph;
        else graph->~Graph();
        graph = NULL;
        if(!my_chunk) delete this;
        else my_chunk->decrement_allocations();
        this_guard = NULL;
    }
}

tbb::task* RootIterBKTask::execute() {
    tbb::task *next = NULL;
    if(!isContinuation) next = SpawnTask(); else cout << "Number of tasks: " << taskIdCnt << "\n";
    return next;
}

tbb::task* RootIterBKTask::SpawnTask() {
    Graph* graph = graphg->graph;
    this->set_ref_count(1 + graphg->graph->getNodeNo());
    tbb::task *nextTask = NULL;
    parallel_for( blocked_range<int>(0, graphg->graph->getNodeNo(), 1), [&](const blocked_range<int>& r) {
          for(int i = r.begin(); i != r.end(); ++i)
              spawn(*new(allocate_child()) MainBKTask(graph->getMappedNode(i), graphg, subgraphBased != 0));
    }, simple_partitioner());
    recycle_as_safe_continuation();
    isContinuation = true;
    return nextTask;
}
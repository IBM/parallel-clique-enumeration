#include <forward_list>
#include <tbb/combinable.h>
#include <tbb/parallel_for.h>
#include <tbb/tick_count.h>
#include <tbb/task_scheduler_init.h>

#include "BKTask.h"
#include "Graph.h"
#include "SetImplementation.h"
#include "utils.h"
#include "MemChunk.h"
#include "MemUsageLogger.h"
#include "UnrolledList.h"

using namespace std;
using namespace tbb;

typedef list<SET_IMPL* > ClqList;
typedef UnrolledList Clique;
extern tbb::combinable<Histogram> pt_hist;

extern bool CollectMemUsage;
extern MemUsageLogger *memLogger;
extern int subgraphBased;
extern bool degeneracyOrd;
extern bool degreeOrd;
extern bool setsMempool;
extern bool deleteSgAfter;
extern int PX_threshold;
extern int mem_threshold;
extern int max_clq_size;
extern unsigned int memBlockSize;
extern ofstream mem_log_stream;

tbb::atomic<unsigned long> BKTask::taskIdCnt = 0;

inline void store_clique(Clique& R) {
    // populate this function according to your needs if you would like the cliques found to be stored
}

inline void delete_set(SET_IMPL*& set) {
    if(set->my_chunk == NULL) {
        delete set; set = NULL;
    }
    else {
        MemChunk* tmp_chunk = set->my_chunk;
        tmp_chunk->decrement_allocations();
        set = NULL;
    }
}

inline void MainBKTask::create_root_sets(SET_IMPL*& P, SET_IMPL*& X) {
    Graph*& graph = graphg->graph;
    SET_IMPL *Q = graph->getAdjacentNodes(new_vertex);

    P = SET_IMPL::create_set(NULL, MemType::ROOT_SET);
    X = SET_IMPL::create_set(NULL, MemType::ROOT_SET);

    Q->for_each([&](int vertex_name) {
        // Determine if the node is in P or X
        if (degeneracyOrd) {
            if (graph->getNodePosition(vertex_name) > graph->getNodePosition(new_vertex))
                P->add_elem(vertex_name);
            else
                X->add_elem(vertex_name);
        } else {
            int this_vertex_size = graph->getAdjacentNodes(vertex_name)->size();
            int other_vertex_size = graph->getAdjacentNodes(new_vertex)->size();
            if ( (degreeOrd && (this_vertex_size > other_vertex_size) || !degreeOrd && (this_vertex_size < other_vertex_size))
                 || (this_vertex_size == other_vertex_size && vertex_name > new_vertex))
                P->add_elem(vertex_name);
            else
                X->add_elem(vertex_name);
        }
    });
}
/************* execute *******************/
tbb::task* MainBKTask::execute() {
    if (!isContinuation)
        return SpawnTask();
    else
        return Continuation();
}

/************* Spawn Task *******************/
tbb::task* MainBKTask::SpawnTask() {
    if(newTask) {
        if(!parent) {
            R_task = new Clique(NULL, MemType::ROOT_CLIQUE);
            R_task->push_back(new_vertex);
            create_root_sets(P_task, X_task);
        }

        bool end = StartTask(*R_task, P_task, X_task, graphg, cand_task);
        this->set_ref_count(1);
        if(end) {
            recycle_as_safe_continuation();
            isContinuation = true;
            return NULL;
        }
        newTask = false;
    }

    int current_node = cand_task->get_next();
    tbb::task *a = NULL;

    if(current_node != -1) {
        a = LoopIteration(current_node, *R_task, P_task, X_task, graphg);
        if(!cand_task->end_iter()) {
            recycle_to_reexecute();
        }
        else {
            delete_set(P_task); delete_set(X_task); delete_set(cand_task);
            if(subgraphBased == 3 || subgraphBased == 2 && NewGraph) graphg->dec_graph_ref_count(graphg);
            recycle_as_safe_continuation();
            isContinuation = true;
        }
    }
    else {
        delete_set(P_task); delete_set(X_task); delete_set(cand_task);
        if(subgraphBased == 3 || subgraphBased == 2 && NewGraph) graphg->dec_graph_ref_count(graphg);
        recycle_as_safe_continuation();
        isContinuation = true;
    }

    // Execute first child recursive call in DFS order
    if(a) this->increment_ref_count();
    return a;
}
/************* Continuation *******************/

inline tbb::task* MainBKTask::Continuation() {
    if(returnClique) {
        R_task->pop_back();
        parent->R_task = R_task;
        R_task = NULL;
    }

    if(subgraphBased == 1 && NewGraph) graphg->dec_graph_ref_count(graphg);

    if(sets_chunk) delete sets_chunk;
    sets_chunk = NULL;
    return NULL;
}

/************* Sequential Run *******************/
inline tbb::task* MainBKTask::SequentialRun(Clique& R, SET_IMPL*& P, SET_IMPL*& X, GraphGuard* gg) {
    SET_IMPL *cand = NULL;
    bool end = StartTask(R, P, X, gg, cand);
    if(end) return NULL;

	/// Recursing
    #pragma forceinline recursive
	cand->for_each([&](int current_node) {
        tbb::task* a = LoopIteration(current_node, R, P, X, gg);
        if(a) { increment_ref_count(); spawn(*a); }
	});

	delete_set(cand); delete_set(P); delete_set(X);
	if(subgraphBased == 3) gg->dec_graph_ref_count(gg);
	return NULL;

}

/************* Start Task *******************/

inline bool MainBKTask::StartTask(Clique& R, SET_IMPL*& P, SET_IMPL*& X, GraphGuard*& gg, /*out*/ SET_IMPL*& cand) {
    // Exiting recursion
    if(P->empty()) {
        if(X->empty()) {
            auto& my_hist = pt_hist.local();
            int r_size = R.size();

            if(my_hist.find(r_size) == my_hist.end())
                my_hist[r_size] = 0;

            my_hist[r_size]++;
            store_clique(R);
        }

        delete_set(P); delete_set(X);
        bool isRootSg = (!parent) && (taskSpawnCnt == 0);
        if((subgraphBased == 3 || (subgraphBased == 2 && taskSpawnCnt == 0)) && !isRootSg) gg->dec_graph_ref_count(gg);
        if(taskSpawnCnt == 0) NewGraph = false;
        return true;
    }

    if(max_clq_size != -1 && R.size() >= max_clq_size) return true;

    MemChunk* sets_chunk_ptr = NULL;
    if(setsMempool) sets_chunk_ptr = sets_chunk;

    /// Create graph
    GraphGuard *thisgg = NULL;
    bool isRootSg = (!parent) && (taskSpawnCnt == 0);
    bool createGraph = (subgraphBased == 1 && isRootSg)||(subgraphBased == 2 && taskSpawnCnt == 0 && NewGraph)||(subgraphBased == 3);
    if(createGraph)
    {
        int memType = !isRootSg ? MemType::SUBGRAPH : MemType::ROOT_SUBGRAPH;

        Graph *Subgraph = gg->graph->createHpxSubgraph(R.back(), P, X, NULL, memType);
        thisgg = new GraphGuard(Subgraph);

        if(!isRootSg && subgraphBased != 1) gg->dec_graph_ref_count(gg);
        gg = thisgg;
        if(subgraphBased != 3) graphg = thisgg;
    }

    Graph*& graph = gg->graph;

    /// Choose pivot
    int pivot = -1;
    if((NewGraph && taskLevel == 0) || (subgraphBased == 3)) pivot = graph->pivot_node;
    else pivot = graph->getPivot(P, X);
    assert(pivot != -1);

    if(setsMempool && P->size() + X->size() < mem_threshold) {
        if(!sets_chunk) sets_chunk = new MemChunk(memBlockSize);
        sets_chunk_ptr = sets_chunk;
    }
    else {
        sets_chunk_ptr = NULL;
    }

    if (sets_chunk_ptr) sets_chunk_ptr->increment_allocations();
    cand = P->exclude(graph->getAdjacentNodes(pivot), sets_chunk_ptr, MemType::SET);

    return false;
}

/************* Loop Iteration *******************/

inline tbb::task* MainBKTask::LoopIteration(int vertex, Clique& R, SET_IMPL*& P, SET_IMPL*& X, GraphGuard*& gg, bool seq) {
    Graph*& graph = gg->graph;
    MemChunk* sets_chunk_ptr = NULL;
    if(setsMempool && P->size() + X->size() < mem_threshold) {
        if(!sets_chunk) sets_chunk = new MemChunk(memBlockSize);
        sets_chunk_ptr = sets_chunk;
    }

    SET_IMPL *intersP = NULL;
    SET_IMPL *intersX = NULL;

    /// Intersect P and X with the adjacency list
    if (sets_chunk_ptr) sets_chunk_ptr->increment_allocations();
    intersP = P->intersect(graph->getAdjacentNodes(vertex), sets_chunk_ptr, MemType::SET);
    if (sets_chunk_ptr) sets_chunk_ptr->increment_allocations();
    intersX = X->intersect(graph->getAdjacentNodes(vertex), sets_chunk_ptr, MemType::SET);

    // Move vertex from P to X
    P->del_elem(vertex);
    X->add_elem(vertex);

    tbb::task *a = NULL;
    if(intersP->size() + intersX->size() < PX_threshold)
    {
        taskSpawnCnt++;
        taskLevel++;
        R.push_back(vertex);

        if(subgraphBased == 3) gg->inc_graph_ref_count();
        a = SequentialRun(R, intersP, intersX, gg);

        R.pop_back();
        taskLevel--;
    }
    else
    {
        Clique *Rcpy = new Clique(R, MemType::CLIQUE);
        Rcpy->push_back(vertex);
        a = new(allocate_child()) MainBKTask(Rcpy, intersP, intersX, gg, /*New Graph*/ (subgraphBased > 1), /*parent*/ this);
        if(subgraphBased > 1) gg->inc_graph_ref_count();
    }

    return a;
}

/********************************************************************************/
/**************** Top level function for parallel BK ****************************/
/********************************************************************************/
void Graph::BronKerboschDegeneracy(int nthr) {
    tbb::task_scheduler_init init(nthr);
    GraphGuard *gg = new GraphGuard(this);
    BKTask &rt = *new(tbb::task::allocate_root()) RootIterBKTask(gg);
    tbb::task::spawn_root_and_wait(rt);
    delete gg;
}
#ifndef _BK_TASK_H_
#define _BK_TASK_H_

#include <ctime>
#include <cstdlib>

#include <tbb/task.h>
#include <tbb/spin_mutex.h>
#include <tbb/mutex.h>
#include <tbb/atomic.h>

#include "Graph.h"
#include "SetImplementation.h"
#include "utils.h"
#include "MemChunk.h"
#include "UnrolledList.h"

using namespace std;

typedef list<SET_IMPL* > ClqList;
typedef UnrolledList Clique;

extern unsigned int memBlockSize;
extern bool setsMempool;

typedef tbb::spin_mutex CoutMutexType;

/****************** Graph Guard ***********************/
struct GraphGuard {
    GraphGuard(Graph *g, MemChunk* chunk = NULL) : graph(g), my_chunk(chunk), ref_count(1) {
        if(chunk) chunk->increment_allocations();
    }

    void inc_graph_ref_count();
    void dec_graph_ref_count(GraphGuard*& this_guard);
    Graph* graph;
    MemChunk* my_chunk;
    int ref_count;
    typedef tbb::spin_mutex RefCountMutexType;
    RefCountMutexType RefCountMutex;

    ~GraphGuard(){}
};

/****************** BK Task ***********************/
class BKTask : public tbb::task {
public:
    BKTask(GraphGuard *g): taskId(taskIdCnt++), graphg(g) {}
    virtual ~BKTask(){}
    virtual tbb::task* execute() override { auto *t = SpawnTask(); return t; }
    void print_tasks_timestamp();
protected:
    static tbb::atomic<unsigned long> taskIdCnt;
    int taskId;
    virtual tbb::task* SpawnTask() = 0;
    GraphGuard *graphg;
};

/****************** Root Iter Task ***********************/

class RootIterBKTask : public BKTask {
public:
    RootIterBKTask(GraphGuard *g): BKTask(g), isContinuation(false) {}
    virtual tbb::task* execute() override;
protected:
    virtual tbb::task* SpawnTask() override;
    bool isContinuation;
};

/****************** Main BK Task ***********************/
class MainBKTask : public BKTask
{
public:
    MainBKTask(Clique *_r, SET_IMPL *_p, SET_IMPL *_x, GraphGuard *gg, bool _ir = false,
               MainBKTask* _par = NULL, bool _ret_clq = false);

    MainBKTask(int vertex, GraphGuard*& g, bool _ir = false);

    virtual ~MainBKTask() {
        if(R_task) delete R_task;
        if(CollectMemUsage) {
                if(parent) memLogger->delTmpMem(sizeof(MainBKTask), MemType::TASK);
                else memLogger->delTmpMem(sizeof(MainBKTask), MemType::ROOT_TASK);
        }
    }

protected:
    virtual tbb::task* SpawnTask();
    tbb::task* Continuation();

    tbb::task* SequentialRun(Clique& R, SET_IMPL*& P, SET_IMPL*& X, GraphGuard* Hpx);
    bool StartTask(Clique& R, SET_IMPL*& P, SET_IMPL*& X, GraphGuard*& gg, /*out*/ SET_IMPL*& cand);
    tbb::task* LoopIteration(int vertex, Clique& R, SET_IMPL*& P, SET_IMPL*& X, GraphGuard*& gg, bool seq = false);

    virtual tbb::task* execute() override;
    void create_root_sets(SET_IMPL*& P, SET_IMPL*& X);

    int taskSpawnCnt;
    int taskLevel;

    int new_vertex;

    // Parameters
    Clique *R_task;
    SET_IMPL *P_task, *X_task;
    SET_IMPL *cand_task;
    bool returnClique;

    bool isContinuation;
    bool newTask;

    long spawnCnt;
private:
    bool NewGraph;
    MemChunk* sets_chunk;
    MainBKTask* parent;

    typedef tbb::spin_mutex MainTaskMutexType;
    MainTaskMutexType MainTaskMutex;
};

inline MainBKTask:: MainBKTask(Clique *_r, SET_IMPL *_p, SET_IMPL *_x, GraphGuard *gg,
        bool _ir, MainBKTask* _par, bool _ret_clq):
    BKTask(gg), taskSpawnCnt(0), taskLevel(0), R_task(_r), P_task(_p), X_task(_x),
    cand_task(NULL), NewGraph(_ir), spawnCnt(0), returnClique(_ret_clq), new_vertex(_r->back()),
    isContinuation(false), newTask(true), sets_chunk(NULL), parent(_par)
{
    if(CollectMemUsage)
    {
        if(parent) memLogger->addTmpMem(sizeof(MainBKTask), MemType::TASK);
        else memLogger->addTmpMem(sizeof(MainBKTask), MemType::ROOT_TASK);
    }
}

inline MainBKTask::MainBKTask(int vertex, GraphGuard*& g, bool _ir):
    BKTask(g), taskSpawnCnt(0), taskLevel(0), R_task(NULL), P_task(NULL),
    X_task(NULL), cand_task(NULL), NewGraph(_ir), spawnCnt(0), returnClique(false),
    new_vertex(vertex), isContinuation(false), newTask(true), sets_chunk(NULL), parent(NULL)
{
    if(CollectMemUsage){
        if(parent) memLogger->addTmpMem(sizeof(MainBKTask), MemType::TASK);
        else memLogger->addTmpMem(sizeof(MainBKTask), MemType::ROOT_TASK);
    }
}
#endif//_BK_TASK_H_
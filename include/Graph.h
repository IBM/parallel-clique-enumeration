#ifndef GRAPH_H
#define GRAPH_H

#include <vector>
#include <tbb/combinable.h>
#include <unordered_map>
#include "SetImplementation.h"
#include "utils.h"
#include "MemUsageLogger.h"

using namespace std;

extern MemUsageLogger *memLogger;
extern bool CollectMemUsage;

class BKTask;

// histogram for a single thread
typedef unordered_map<int, long> Histogram;

class Graph {
public:
	// Constructor
	Graph(int V = 0, MemChunk* chunk = NULL, int mtype = MemType::GRAPH);
	Graph(string path);

	// Destructor
	~Graph();

	// function that adds edge to graph
	void addEdge(int v, int w);

	// Get necessary data
	int getNodeNo() { return nodeNo; }
	SET_IMPL* getAdjacentNodes(int node) {	return adjList[node]; }
	void setAdjacentNodes(int node, SET_IMPL* alist);

	int getMappedNode(int node) { return vertexOrdering[node]; };
	int getNodePosition(int node) {	return backwardsMapping[node]; }

	Graph* createHpxSubgraph(int current_node, SET_IMPL*& P, SET_IMPL*& X, MemChunk *chunk = NULL, int mem_type = MemType::SUBGRAPH);
	int getPivot(SET_IMPL*& P, SET_IMPL*& X);

	void initFromFile(string path);

	int degeneracyOrdering();

	// Maximal Clique Enumeration related stuff
	void BronKerbosch();
	void BronKerboschDegeneracy(int nthr = 256);

    void writeCliqueHist(tbb::combinable<Histogram>& pt_hist);

	friend class BKTask;
    int mem_type;
    MemChunk *my_chunk;
    int maxdeg, degeneracy, pivot_node;
private:
	long nodeNo, edgeNo;
    long mem_usage;

    unordered_map<int, SET_IMPL*> adjList;
    vector<int> vertexOrdering;
    unordered_map<int, int> backwardsMapping;
};

inline Graph::Graph(int V, MemChunk *chunk, int mtype) :
        nodeNo(V), edgeNo(0), maxdeg(-1), degeneracy(-1),
        pivot_node(-1), mem_usage(0), mem_type(mtype), my_chunk(chunk)
{
    if(CollectMemUsage && !chunk) memLogger->addTmpMem(sizeof(*this), mem_type);
	adjList.reserve(nodeNo);
}

inline Graph::Graph(string path) :
    nodeNo(0), edgeNo(0), maxdeg(-1), degeneracy(-1),
    pivot_node(-1), mem_usage(0), mem_type(MemType::GRAPH), my_chunk(NULL)
{
    if(CollectMemUsage) memLogger->addTmpMem(sizeof(*this), mem_type);
	initFromFile(path);
}

inline Graph::~Graph() {
   if(my_chunk == NULL) {
        if(CollectMemUsage) memLogger->delTmpMem(sizeof(*this), mem_type);
        for (const auto &pair : adjList) {
            SET_IMPL *const &list = pair.second;
            if (list) delete list;
        }
    }
    else my_chunk->decrement_allocations();
}

inline void Graph::addEdge(int v, int w) {
    if(adjList.find(v) == adjList.end()) adjList[v] = SET_IMPL::create_set(NULL, mem_type);
    if(adjList.find(w) == adjList.end()) adjList[w] = SET_IMPL::create_set(NULL, mem_type);
    if(v == w) return; // Prevent loops
	adjList[v]->add_elem(w); adjList[w]->add_elem(v);

	edgeNo+=2;
	if(adjList[v]->size() > maxdeg) maxdeg = adjList[v]->size();
	if(adjList[w]->size() > maxdeg) maxdeg = adjList[w]->size();
}

inline void Graph::setAdjacentNodes(int node, SET_IMPL* alist) {
	adjList[node] = alist;
	edgeNo += alist->size();
	if(alist->size() > maxdeg) maxdeg = alist->size();
}
#endif
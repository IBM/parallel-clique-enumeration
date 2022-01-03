#include <iostream>
#include <fstream>
#include <sstream>
#include <map>
#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <algorithm>
#include <iterator>

#include "Graph.h"
#include "MemChunk.h"
#include "BKTask.h"

using namespace std;
extern bool CollectMemUsage;

Graph* Graph::createHpxSubgraph(int current_node, SET_IMPL*& P, SET_IMPL*& X, MemChunk *chunk, int mem_type) {
	int numberOfElems = P->size() + X->size();
	Graph *Subgraph = NULL;
	if(!chunk) Subgraph = new Graph(numberOfElems, NULL, mem_type);
	else Subgraph = new( chunk->get_address(sizeof(Graph)) ) Graph(numberOfElems, chunk, mem_type);

    int max_neighbors = -1;
	// P can be connected to either P or X
	P->for_each([&](int node){
		SET_IMPL* overlapping_nodes = P->intersect(this->getAdjacentNodes(node), chunk, Subgraph->mem_type);
		Subgraph->setAdjacentNodes(node, overlapping_nodes);
		if(overlapping_nodes->size() > max_neighbors) {
			max_neighbors = overlapping_nodes->size();
			Subgraph->pivot_node = node;
		}
	});

	// X can be only connected to P
	X->for_each([&](int node){
        SET_IMPL* overlapping_nodes = P->intersect(this->getAdjacentNodes(node), chunk, Subgraph->mem_type);
		Subgraph->setAdjacentNodes(node, overlapping_nodes);
		// Determining pivot
		if(overlapping_nodes->size() > max_neighbors)
		{
			max_neighbors = overlapping_nodes->size();
			Subgraph->pivot_node = node;
		}
		// Adding x nodes to p
		overlapping_nodes->for_each([&](int xnode){
			auto& list = Subgraph->adjList[xnode];
			list->add_elem(node);
			Subgraph->edgeNo++;
			if(list->size() > Subgraph->maxdeg)
			    Subgraph->maxdeg = list->size();
		});
	});

    #ifdef HASH_JOIN_SET_IMPL
    // Hash the adjacency lists
    for (const auto &pair : Subgraph->adjList) {
            SET_IMPL *const &list = pair.second;
            if(list) list->hashSet();
        }
    #endif
	return Subgraph;
}

int Graph::getPivot(SET_IMPL*& P, SET_IMPL*& X) {
	int pivot = P->get_first();
	int maxint = 0;
	auto pivot_selection = [&](int node){
		SET_IMPL* adj_nodes = this->getAdjacentNodes(node);
		int sz = P->intersection_size(adj_nodes);
        if( sz > maxint ) { pivot = node; maxint = sz; }
	};
	X->for_each(pivot_selection);
	P->for_each(pivot_selection);
	return pivot;
}

// O(n + m) function for finding the degeneracy ordering
int Graph::degeneracyOrdering() {
	int degen = 0;
	vector<unordered_set<int>> degArray(maxdeg+1);
	unordered_map<int, int> nodeDegrees(nodeNo); // Mappint between node and degree

	for(const auto& pair : adjList) {
		const int& i = pair.first;
		SET_IMPL* const& list = pair.second;
		assert(list->size() < maxdeg+1);
		degArray[list->size()].insert(i);
		nodeDegrees[i] = list->size();
	}

	for(int curnode = 0; curnode < nodeNo; curnode ++) {
		for(int i = 0; i < maxdeg+1; i++) {
			if(!degArray[i].empty()) {
				auto first = degArray[i].begin();
				int nextNode = *first;
				degArray[i].erase(first);

				vertexOrdering[curnode] = nextNode;
				backwardsMapping[nextNode] = curnode;
				degen = max(degen, i);

				adjList[nextNode]->for_each([&](int n){
					int degIndex = nodeDegrees[n];
					auto it = degArray[degIndex].find(n);
					if(it != degArray[degIndex].end()) {
							degArray[degIndex].erase(it);
							if(degIndex != 0) degArray[degIndex-1].insert(n);
							nodeDegrees[n]--;
					}
				});
				break;
			}
		}
	}
	degeneracy = degen;
	return degen;
}

void Graph::initFromFile(string path) {
	ifstream graphFile(path);
    nodeNo = -1;
    bool firstRow = true;
	while(true) {
		string line; getline(graphFile, line);
		if(graphFile.eof()) break;
		if(line[0] == '%' || line[0] == '#') continue;
		stringstream ss(line);
		int first, second;
		ss >> first >> second;
		if(firstRow) {
            firstRow = false;
		    // if first row has 3 numbers, it contains number of nodes
		    if(!ss.eof()){
		        int third = -1; ss >> third;
		        if(third != -1) { this->nodeNo = first; adjList.reserve(nodeNo); continue; }
		    }
        }
		addEdge(first, second);
	}
	if(nodeNo = -1) nodeNo = adjList.size();
	assert(nodeNo == adjList.size());
    backwardsMapping.reserve(nodeNo);
    vertexOrdering.reserve(nodeNo);
    vertexOrdering.resize(nodeNo);
    int it = 0;
    for(const auto& pair : adjList){
        vertexOrdering[it] = pair.first;
        backwardsMapping[pair.first] = it;
        it++;
    }
#ifdef HASH_JOIN_SET_IMPL
    // Hash the adjacency lists
    for (const auto &pair : adjList) {
            SET_IMPL *const &list = pair.second;
            if(list) list->hashSet();
        }
#endif
	cout << "#Vertex = " << nodeNo << "; #Edge = " << edgeNo/2 << endl;
	graphFile.close();
}

void Graph::writeCliqueHist(tbb::combinable<Histogram>& pt_hist) {
	map<int,int> histogram;
    long maxClqNum = 0;
	pt_hist.combine_each([&](Histogram hist) {
        for(auto& pair : hist) {
            const int& clq_size = pair.first;
            const long& clq_num = pair.second;
            maxClqNum += clq_num;
            if(histogram.find(clq_size) == histogram.end()) histogram[clq_size] = 0;
            histogram[clq_size] += clq_num;
        }
	});
	cout << "# Number of maximal cliques: " << maxClqNum << "\n";
	cout << "# Clique histogram:\n";
	cout << "# clique_size, num_of_cliques\n";
	for(auto hist : histogram)  cout << hist.first << ", " << hist.second << "\n";
}

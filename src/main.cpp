#include <iostream>
#include <fstream>

#include <immintrin.h>
#include <cstdint>
#include <sys/stat.h>
#include <limits.h>
#include <unistd.h>
#include <vector>
#include <tbb/combinable.h>

#include "Graph.h"
#include "utils.h"
#include "MemUsageLogger.h"

#define DATAPATH string("data/")
#define RESPATH  string("results/cliques/")
#define SETSTAT_PATH string("results/setstat/")

using namespace std;
tbb::combinable<Histogram> pt_hist;

// Memory usage
ofstream mem_log_stream;
bool CollectMemUsage = false;
MemUsageLogger *memLogger = NULL;
bool degeneracyOrd = true;
bool degreeOrd = false;
bool setsMempool = false;
int PX_threshold = 30;
int mem_threshold = 20;
int max_clq_size = -1;
int subgraphBased = 0;
unsigned int memBlockSize = 20480;

int main(int argc, char** argv) {
    if(cmdOptionExists(argv, argv+argc, "-h") || cmdOptionExists(argv, argv+argc, "--help")) {
        printHelp(); return 0;
    }

    char* rp = getenv("PROJECT_ROOT");
	string root_path;
	if(rp != NULL) root_path = string(rp); else root_path = string("..");

    // Read path
	string path = root_path + "/" + DATAPATH + "simple.mtx";
    if(cmdOptionExists(argv, argv+argc, "-f")) path = string(getCmdOption(argv, argv + argc, "-f"));
	string name = path.substr(path.find_last_of("/"), path.find_last_of(".")-path.find_last_of("/"));

    // TODO: Check if the file exists
    struct stat buffer;
    if(stat(path.c_str(), &buffer) != 0) {
        cout << "The input file doesn't exist" << endl; return 0;
    }

    // Read number of threads
	int nthr = 256;
    if(cmdOptionExists(argv, argv+argc, "-n")) nthr = stoi(string(getCmdOption(argv, argv + argc, "-n")));

    long sampling_int = 100000;
    if(cmdOptionExists(argv, argv+argc, "-o")) {
        setsMempool = true;
        if(cmdOptionExists(argv, argv+argc, "-b")) memBlockSize = stoi(string(getCmdOption(argv, argv + argc, "-b")));
    }

    if(cmdOptionExists(argv, argv+argc, "-i")) sampling_int = stol(string(getCmdOption(argv, argv + argc, "-i")));

    if(cmdOptionExists(argv, argv+argc, "-m")) {
        CollectMemUsage = true;
        memLogger = new MemUsageLogger(string(getCmdOption(argv, argv + argc, "-m")), sampling_int);
    }

    if(cmdOptionExists(argv, argv+argc, "--ord")) {
        int ord = stol(string(getCmdOption(argv, argv + argc, "--ord")));
        degeneracyOrd = (ord == 0); degreeOrd = (ord == 1);
    }

    if(cmdOptionExists(argv, argv+argc, "--thresh")) PX_threshold = stoi(string(getCmdOption(argv, argv + argc, "--thresh")));
    if(cmdOptionExists(argv, argv+argc, "--mem-thresh")) mem_threshold = stoi(string(getCmdOption(argv, argv + argc, "--mem-thresh")));
    if(cmdOptionExists(argv, argv+argc, "--max-clq")) max_clq_size = stoi(string(getCmdOption(argv, argv + argc, "--max-clq")));
    if(cmdOptionExists(argv, argv+argc, "-s")) {
        subgraphBased = stoi(string(getCmdOption(argv, argv + argc, "-s")));
        if(subgraphBased > 2 || subgraphBased < 0) subgraphBased == 0;
    }

	auto tick0 = tbb::tick_count::now();
	string extension = path.substr(path.find_last_of(".")+1);
	Graph *g = new Graph;
	g->initFromFile(path);
	auto tick1 = tbb::tick_count::now();

	cout << "Graph read time: " << (tick1-tick0).seconds() << "s" << endl;
	cout << "Bron Kerbosch for " << path<< endl;

    if(degeneracyOrd) {
        tick0 = tbb::tick_count::now();
        g->degeneracyOrdering();
        tick1 = tbb::tick_count::now();
        std::cout << "Degeneracy = " << g->degeneracy << " Ordering in: " << (tick1 - tick0).seconds() << "s" << endl;
    }

    tick0 = tbb::tick_count::now();
    g->BronKerboschDegeneracy(nthr);
    tick1 = tbb::tick_count::now();
    auto bk_time = (tick1 - tick0).seconds();
    if (CollectMemUsage) memLogger->printData();
    cout << "Maximal clique enumeration time: " << bk_time << "s" << endl;

	// Write to the output file
    if(cmdOptionExists(argv, argv+argc, "-p")) g->writeCliqueHist(pt_hist);
    delete g; g = NULL;
    if(memLogger) delete memLogger; memLogger = NULL;

    return 0;
}

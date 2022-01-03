# Parallel clique enumeration

This is the source code for the VLDB 2020 paper titled "Manycore Clique Enumeration with Fast Set Intersections"

This project is a collaborative effort between IBM Research Europe - Zurich and EPFL, partly funded by SNSF (http://p3.snf.ch/Project-172610)

The contact email address: jov@zurich.ibm.com

### Building

Prerequisites for building our code:

Compiler: GCC version 7 or higher  
Intel Threading Building Blocks 2020 Update 2, available here: https://github.com/oneapi-src/oneTBB/releases/tag/v2020.2

Before building our code, tbb needs to be enabled:

```
source /path-to-tbb/bin/tbbvars.sh intel64 linux auto_tbbroot
```

Our code can be build using the following commands:

```
mkdir build
cd build
cmake .. -DCMAKE_C_COMPILER=`which gcc` -DCMAKE_CXX_COMPILER=`which g++` -DCMAKE_BUILD_TYPE=Release
make
```

Our vectorized algorithm also requires the AVX512 vector instruction set, which can be verified using the following command:
```
lscpu | grep avx512
```
If the target CPU does not support the AVX512 instruction set, non-vectorized version of our algorithm can be executed by commenting out the line 17 from file `hash-join-mce/include/utils.h` and removing flags `-mavx512f` and `-xMIC-AVX512` from file `hash-join-mce/CMakeLists.txt`.

### Executing

To run our code, simply execute:

```
./mce -f <graph_path>
```

The most important command line options are:

```
-f                Path to the input file graph
-p                Prints the resulting clique histogram, no argument
-n                Number of threads, default 256
-o                Turns on memory allocation grouping, no argument
```
To access other command line options use `./mce -h`

Optionally, for better performance, enable using huge pages in TBB scalable allocator:
```
export TBB_MALLOC_USE_HUGE_PAGES=1
```
And interleave the memory across different NUMA nodes using `numactl` command:
```
numactl -i all ./mce -f <graph_path> [other_options]
```

The file containing the input graph contains the list of edges, one edge per line. Each edge is represented with the identifiers of the two vertices that it connects. 
Optionally, the first row might contain three numbers in format `n n m`, where `n` is the number of vertices and `m` number of edges. 
Graph dataset can be obtained from Network Data Repository https://networkrepository.com/ and SNAP https://snap.stanford.edu/data/.

The sample input graph:
```
6 6 7
1 2
1 5
2 3
2 5
3 4
4 5
4 6
```

An example of running our code:
```
numactl -i all ./mce -f ../data/simple.mtx -p -o
```

Which outputs the following clique histogram:
```
# Number of maximal cliques: 5
# Clique histogram:
# clique_size, num_of_cliques
2, 4
3, 1
```

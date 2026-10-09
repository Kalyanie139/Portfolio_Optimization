CXX ?= g++
CXXFLAGS = -std=c++17 -O3

all: bf_openmp branch_bound_parallel branch_bound_seq sequential_exhaustive

bf_openmp: src/bf_openmp.cpp ; $(CXX) $(CXXFLAGS) -fopenmp $< -o $@

branch_bound_parallel: src/branch_bound_parallel.cpp ; $(CXX) $(CXXFLAGS) -fopenmp $< -o $@

branch_bound_seq: src/branch_bound_seq.cpp ; $(CXX) $(CXXFLAGS) $< -o $@

sequential_exhaustive: src/sequential_exhaustive.cpp ; $(CXX) $(CXXFLAGS) $< -o $@

clean: ; rm -f bf_openmp branch_bound_parallel branch_bound_seq sequential_exhaustive

.PHONY: all clean

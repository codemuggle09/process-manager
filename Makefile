CXX = g++
SRC = src/process-manager.cpp
OUT = bin/process_manager
CXXFLAGS = -std=c++11

all:
	$(CXX) $(SRC) -o $(OUT) $(CXXFLAGS)

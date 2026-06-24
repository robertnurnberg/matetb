CXX = g++
CXXFLAGS = -std=c++20 -Wall -Wextra -O3 -g -march=native

HEADERS = misc.hpp options.hpp matetb.hpp
EXT_HEADERS = external/chess.hpp external/argparse.hpp
EXT_HEADERS2 = $(EXT_HEADERS) external/threadpool.hpp external/parallel_hashmap/phmap.h

EXE_FILE = matetb
EXE_FILE2 = matetb_threaded

.PHONY: all clean format

all: $(EXE_FILE) $(EXE_FILE2)

$(EXE_FILE): matetb.cpp $(HEADERS) $(EXT_HEADERS)
	$(CXX) $(CXXFLAGS) -o $@ $<

$(EXE_FILE2): matetb_threaded.cpp $(HEADERS) $(EXT_HEADERS2)
	$(CXX) $(CXXFLAGS) -o $@ $<

format:
	clang-format -i $(HEADERS) matetb.cpp matetb_threaded.cpp

clean:
	rm -f $(EXE_FILE) $(EXE_FILE2)

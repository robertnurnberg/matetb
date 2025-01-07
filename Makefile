CXX = g++
CXXFLAGS = -Wall -Wextra -O3 -g -march=native

SRC_FILES = matetb.hpp matetb.cpp
EXE_FILE = matetb
EXT_HEADERS = external/chess.hpp external/argparse.hpp

SRC_FILES2 = matetb.hpp matetb_threaded.cpp
EXE_FILE2 = matetb_threaded
EXT_HEADERS2 = external/chess.hpp external/argparse.hpp external/threadpool.hpp external/parallel_hashmap/phmap.h

.PHONY = all clean format

all: $(EXE_FILE) $(EXE_FILE2)

$(EXE_FILE): $(SRC_FILES) $(EXT_HEADERS)
	$(CXX) $(CXXFLAGS) -o $(EXE_FILE) $(SRC_FILES)

$(EXE_FILE2): $(SRC_FILES2) $(EXT_HEADERS)
	$(CXX) $(CXXFLAGS) -std=c++20 -o $(EXE_FILE2) $(SRC_FILES2)

format:
	clang-format -i $(SRC_FILES) $(SRC_FILES2)

clean:
	rm -f $(EXE_FILE) $(EXE_FILE2)

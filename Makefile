CXX = g++
CXXFLAGS = -Wall -Wextra -std=c++17 -O3 -g -march=native

SRC_FILES = matetb.hpp matetb.cpp
EXE_FILE = matetb
EXT_HEADERS = external/chess.hpp external/argparse.hpp

all: $(EXE_FILE)

$(EXE_FILE): $(SRC_FILES) $(EXT_HEADERS)
	$(CXX) $(CXXFLAGS) -o $(EXE_FILE) $(SRC_FILES)

format:
	clang-format -i $(SRC_FILES)

clean:
	rm -f $(EXE_FILE)

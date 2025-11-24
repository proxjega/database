# Compiler settings
CXX = g++
# -I. includes current directory (for common.hpp)
# -I../btree/include includes the logger header location
# -std=c++17 is required for <filesystem> used in logger.hpp
CXXFLAGS = -std=c++17 -Wall -Wextra -pthread -I. -I../btree/include

# Helper variables for paths
BTREE_SRC_DIR = ../btree/src
BTREE_OBJ = logger.o

# Targets to build
all: leader follower client run

# 1. Compile the Logger implementation from the sibling directory
# We compile it into an object file (.o) so we don't have to recompile it for every executable
$(BTREE_OBJ): $(BTREE_SRC_DIR)/logger.cpp
	$(CXX) $(CXXFLAGS) -c $(BTREE_SRC_DIR)/logger.cpp -o $(BTREE_OBJ)

# 2. Build the Leader executable
# Depends on leader.cpp, the logger object file, and common.hpp
leader: leader.cpp $(BTREE_OBJ) common.hpp
	$(CXX) $(CXXFLAGS) leader.cpp $(BTREE_OBJ) -o leader

# 3. Build the Follower executable
follower: follower.cpp $(BTREE_OBJ) common.hpp
	$(CXX) $(CXXFLAGS) follower.cpp $(BTREE_OBJ) -o follower

# 4. Build the Client executable
client: client.cpp $(BTREE_OBJ) common.hpp
	$(CXX) $(CXXFLAGS) client.cpp $(BTREE_OBJ) -o client

# 5. Build the Run executable (if needed)
run: run.cpp $(BTREE_OBJ) common.hpp
	$(CXX) $(CXXFLAGS) run.cpp $(BTREE_OBJ) -o run

# Clean up build artifacts
clean:
	rm -f leader follower client run *.o
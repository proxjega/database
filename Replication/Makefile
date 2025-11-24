# Compiler settings
CXX = g++
# -I. includes current directory (for common.hpp)
# -I../btree/include includes the B-Tree headers (database.h, logger.hpp, etc.)
CXXFLAGS = -std=c++17 -Wall -Wextra -pthread -I. -I../btree/include

# Path to the B-Tree source code
BTREE_SRC_DIR = ../btree/src

# The object files needed to run the Database engine.
# We need database.o (the main class), logger.o (WAL), and the page implementations.
BTREE_OBJS = database.o logger.o page.o internalpage.o leafpage.o

# Targets to build
all: leader follower client run

# --- Build Rules ---

# 1. Generic rule to compile any .cpp file from ../btree/src into a .o file
# This handles database.cpp, logger.cpp, page.cpp, etc.
%.o: $(BTREE_SRC_DIR)/%.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

# 2. Build the Leader executable
leader: leader.cpp $(BTREE_OBJS) common.hpp
	$(CXX) $(CXXFLAGS) leader.cpp $(BTREE_OBJS) -o leader

# 3. Build the Follower executable
follower: follower.cpp $(BTREE_OBJS) common.hpp
	$(CXX) $(CXXFLAGS) follower.cpp $(BTREE_OBJS) -o follower

# 4. Build the Client executable
client: client.cpp $(BTREE_OBJS) common.hpp
	$(CXX) $(CXXFLAGS) client.cpp $(BTREE_OBJS) -o client

# 5. Build the Run executable
run: run.cpp $(BTREE_OBJS) common.hpp
	$(CXX) $(CXXFLAGS) run.cpp $(BTREE_OBJS) -o run

# Clean up build artifacts
clean:
	rm -f leader follower client run *.o
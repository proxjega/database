# Compiler settings
CXX = g++

# Directories
SRC_DIR = src
INCLUDE_DIR = include
BTREE_SRC_DIR = ../btree/src

# Compiler flags:
# -I$(INCLUDE_DIR)  -> Looks for your headers (common.hpp, rules.hpp) in ./include
# -I../btree/include -> Looks for B-Tree headers
CXXFLAGS = -std=c++17 -Wall -Wextra -pthread -I$(INCLUDE_DIR) -I../btree/include

# The object files needed to run the Database engine (from sibling project).
BTREE_OBJS = database.o logger.o page.o internalpage.o leafpage.o

# Common headers dependencies (if these change, we should recompile)
LOCAL_HEADERS = $(INCLUDE_DIR)/common.hpp $(INCLUDE_DIR)/rules.hpp

# Targets to build
all: leader follower client run

# --- Build Rules ---

# 1. Generic rule to compile any .cpp file from ../btree/src into a .o file
#    (e.g. database.o, logger.o)
%.o: $(BTREE_SRC_DIR)/%.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

# 2. Build the Leader executable
#    Depends on src/leader.cpp and header files
leader: $(SRC_DIR)/leader.cpp $(BTREE_OBJS) $(LOCAL_HEADERS)
	$(CXX) $(CXXFLAGS) $(SRC_DIR)/leader.cpp $(BTREE_OBJS) -o leader

# 3. Build the Follower executable
#    Depends on src/follower.cpp and header files
follower: $(SRC_DIR)/follower.cpp $(BTREE_OBJS) $(LOCAL_HEADERS)
	$(CXX) $(CXXFLAGS) $(SRC_DIR)/follower.cpp $(BTREE_OBJS) -o follower

# 4. Build the Client executable
#    Depends on src/client.cpp and header files
client: $(SRC_DIR)/client.cpp $(BTREE_OBJS) $(LOCAL_HEADERS)
	$(CXX) $(CXXFLAGS) $(SRC_DIR)/client.cpp $(BTREE_OBJS) -o client

# 5. Build the Run executable
#    Depends on src/run.cpp and header files
run: $(SRC_DIR)/run.cpp $(BTREE_OBJS) $(LOCAL_HEADERS)
	$(CXX) $(CXXFLAGS) $(SRC_DIR)/run.cpp $(BTREE_OBJS) -o run

# Clean up build artifacts
# This removes the executables and any .o files created in the root
clean:
	rm -f leader follower client run *.o
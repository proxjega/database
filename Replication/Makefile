CXX = g++

SRC_DIR = src
INCLUDE_DIR = include
BTREE_SRC_DIR = ../btree/src

CXXFLAGS = -std=c++17 -Wall -Wextra -pthread -I$(INCLUDE_DIR) -I../btree/include

BTREE_OBJS = database.o logger.o page.o internalpage.o leafpage.o

LOCAL_HEADERS = $(INCLUDE_DIR)/common.hpp $(INCLUDE_DIR)/rules.hpp

all: leader follower client run

%.o: $(BTREE_SRC_DIR)/%.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

leader: $(SRC_DIR)/leader.cpp $(BTREE_OBJS) $(LOCAL_HEADERS)
	$(CXX) $(CXXFLAGS) $(SRC_DIR)/leader.cpp $(BTREE_OBJS) -o leader

follower: $(SRC_DIR)/follower.cpp $(BTREE_OBJS) $(LOCAL_HEADERS)
	$(CXX) $(CXXFLAGS) $(SRC_DIR)/follower.cpp $(BTREE_OBJS) -o follower

client: $(SRC_DIR)/client.cpp $(BTREE_OBJS) $(LOCAL_HEADERS)
	$(CXX) $(CXXFLAGS) $(SRC_DIR)/client.cpp $(BTREE_OBJS) -o client

run: $(SRC_DIR)/run.cpp $(BTREE_OBJS) $(LOCAL_HEADERS)
	$(CXX) $(CXXFLAGS) $(SRC_DIR)/run.cpp $(BTREE_OBJS) -o run

clean:
	rm -f leader follower client run *.o
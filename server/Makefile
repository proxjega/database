CXX = g++
CXXFLAGS = -std=c++20 -O2 -I./lithium/single_headers -I./include -I../Replication -pthread
SRC = main.cpp src/db_client.cpp
TARGET = server_app
LDFLAGS = -lboost_context -lssl -lcrypto

all:
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(SRC) $(LDFLAGS)

clean:
	rm -f $(TARGET)

run: all
	./server_app & sleep 0.5; ps aux | grep server_app | grep -v grep
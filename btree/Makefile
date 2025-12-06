CXX = g++
CXXFLAGS = -Wall -Wextra -O2 -std=c++17 -g

TARGET = build/main

SRCS = src/main.cpp src/database.cpp src/page.cpp src/leafpage.cpp src/internalpage.cpp src/logger.cpp
OBJS = $(SRCS:.cpp=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)

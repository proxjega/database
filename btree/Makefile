# Compiler and flags
CXX = g++
CXXFLAGS = -Wall -Wextra -O2 -std=c++17 -g -DDEBUG

# Target
TARGET = build/main

# Sources and objects
SRCS = src/main.cpp src/database.cpp src/page.cpp src/leafpage.cpp src/internalpage.cpp src/logger.cpp
OBJS = $(SRCS:.cpp=.o)

# Default rule
all: $(TARGET)

# Link objects into binary
$(TARGET): $(OBJS)
	mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJS)

# Compile source to object
%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Clean up build artifacts
clean:
	rm -f $(OBJS) $(TARGET)

CXX = g++
CXXFLAGS = -std=c++11 -Wall -Wextra -pedantic -pthread

SRCS = test.cpp threadpool.cpp
OBJS = $(SRCS:.cpp=.o)
TARGET = threadpool_test

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) $^ -o $@

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)
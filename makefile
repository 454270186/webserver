CXX = g++
CXXFLAGS = -std=c++14 -Wall -Wextra -pedantic -pthread

SRCS = main.cpp threadpool.cpp http_conn.cpp
OBJS = $(SRCS:.cpp=.o)
TARGET = http_server

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) $^ -o $@

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)
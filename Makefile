CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -O2
LDFLAGS = -lpthread -ldl -lm

# Source files
SRCS = main.cpp ringbuf.cpp
OBJS = $(SRCS:.cpp=.o)
TARGET = multi-audio-broadcast

# Miniaudio is header-only, so no .cpp file for it, but we need to link OS libraries
# On Linux: -lpthread -ldl -lm
# On Windows: -lole32 (if using COM)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)

run: $(TARGET)
	./$(TARGET)

.PHONY: all clean run
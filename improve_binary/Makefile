CXX ?= g++
TARGET ?= improve_binary
SRC_DIR := src

SRCS := $(SRC_DIR)/main.cpp
OBJS := $(SRCS:.cpp=.o)
DEPS := $(OBJS:.o=.d)

CPPFLAGS ?= -MMD -MP
CXXFLAGS ?= -std=c++20 -O3 -Wall -Wextra
LDFLAGS ?=
LDLIBS ?=

.PHONY: all clean debug

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(LDFLAGS) $^ $(LDLIBS) -o $@

%.o: %.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

debug: CXXFLAGS := -std=c++20 -O0 -g -Wall -Wextra
debug: clean all

clean:
	rm -f $(TARGET) $(OBJS) $(DEPS)

-include $(DEPS)

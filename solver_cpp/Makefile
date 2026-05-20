CXX ?= g++
TARGET ?= solver_cpp
SRC_DIR := src

SRCS := \
	$(SRC_DIR)/main.cpp \
	$(SRC_DIR)/Graph.cpp \
	$(SRC_DIR)/Utils.cpp \
	$(SRC_DIR)/Algorithms.cpp

OBJS := $(SRCS:.cpp=.o)
DEPS := $(OBJS:.o=.d)

CPPFLAGS ?= -MMD -MP
CXXFLAGS ?= -std=c++17 -O3
LDFLAGS ?=
LDLIBS ?=

.PHONY: all clean static debug

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(LDFLAGS) $^ $(LDLIBS) -o $@

%.o: %.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

static: clean
	$(MAKE) TARGET=$(TARGET) CXX="$(CXX)" CPPFLAGS="$(CPPFLAGS)" CXXFLAGS="$(CXXFLAGS)" LDFLAGS="$(LDFLAGS) -static -static-libgcc -static-libstdc++" LDLIBS="$(LDLIBS)"

debug: CXXFLAGS := -std=c++17 -O0 -g
debug: clean all

clean:
	rm -f $(TARGET) $(OBJS) $(DEPS)

-include $(DEPS)

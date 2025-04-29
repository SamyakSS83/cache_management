# Makefile for L1 Cache Simulator

CXX = g++
CXXFLAGS = -Wall -Wextra -O2
CXXFLAGS += -g -O1 -fsanitize=address
SRCDIR = src
OBJDIR = obj
BINDIR = bin

# Source files
SOURCES = $(wildcard $(SRCDIR)/*.cpp)
OBJECTS = $(patsubst $(SRCDIR)/%.cpp, $(OBJDIR)/%.o, $(SOURCES))
EXECUTABLE = L1simulate

# Create directories if they don't exist
$(shell mkdir -p $(OBJDIR) $(BINDIR))

all: $(BINDIR)/$(EXECUTABLE)

$(BINDIR)/$(EXECUTABLE): $(OBJECTS)
	$(CXX) $(CXXFLAGS) $^ -o $@

$(OBJDIR)/%.o: $(SRCDIR)/%.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -rf $(OBJDIR)/*.o $(BINDIR)/$(EXECUTABLE)
	rmdir $(OBJDIR) $(BINDIR) 2>/dev/null || true

.PHONY: all clean

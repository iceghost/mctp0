CXX = c++
CXXFLAGS = -xc++ -std=c++26 -I. -isystem ./stdexec/include -g
# CXXFLAGS = -xc++ -std=c++26 -I. -isystem ./stdexec/include -Os
LDFLAGS = -flto
# -fconcepts-diagnostics-depth=2
LDLIBS = -lsystemd

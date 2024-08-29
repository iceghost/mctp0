CXX = c++
CXXFLAGS = -xc++ -std=c++23 -I. -isystem ./stdexec/include -Os
# CXXFLAGS = -xc++ -std=c++26 -I. -isystem ./stdexec/include -Os
LDFLAGS = -flto
# -fconcepts-diagnostics-depth=2
LDLIBS = -lsystemd -lstdc++exp

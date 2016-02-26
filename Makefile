.PHONY: all
all: selftest runtest

selftest: extest.hpp
	$(CXX) -o selftest extest.cpp -DUNITTEST -DEX_SELFTEST -std=c++14 -g $(CXXFLAGS) -rdynamic $(LDFLAGS) -ldl

.PHONY: runtest
runtest:
	./selftest

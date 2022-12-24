all: prac

CXXFLAGS=-march=native -std=c++17 -Wall -Wno-ignored-attributes -ggdb -O3
LDFLAGS=-ggdb
LDLIBS=-lbsd -lboost_system -lboost_context -lboost_chrono -lboost_thread -lpthread

BIN=prac
SRCS=prac.cpp mpcio.cpp preproc.cpp online.cpp mpcops.cpp rdpf.cpp \
    cdpf.cpp
OBJS=$(SRCS:.cpp=.o)
ASMS=$(SRCS:.cpp=.s)

$(BIN): $(OBJS)
	g++ $(LDFLAGS) -o $@ $^ $(LDLIBS)

%.s: %.cpp
	g++ $(CXXFLAGS) -S -o $@ $^

# Remove the files created by the preprocessing phase
reset:
	-rm -f *.p[012].t*

clean: reset
	-rm -f $(BIN) $(OBJS) $(ASMS)

depend:
	makedepend -Y -- $(CXXFLAGS) -- $(SRCS)

# DO NOT DELETE THIS LINE -- make depend depends on it.

prac.o: mpcio.hpp types.hpp preproc.hpp options.hpp online.hpp
mpcio.o: mpcio.hpp types.hpp rdpf.hpp coroutine.hpp bitutils.hpp dpf.hpp
mpcio.o: prg.hpp aes.hpp rdpf.tcc cdpf.hpp cdpf.tcc
preproc.o: types.hpp coroutine.hpp mpcio.hpp preproc.hpp options.hpp rdpf.hpp
preproc.o: bitutils.hpp dpf.hpp prg.hpp aes.hpp rdpf.tcc cdpf.hpp cdpf.tcc
online.o: online.hpp mpcio.hpp types.hpp options.hpp mpcops.hpp coroutine.hpp
online.o: rdpf.hpp bitutils.hpp dpf.hpp prg.hpp aes.hpp rdpf.tcc duoram.hpp
online.o: duoram.tcc cdpf.hpp cdpf.tcc
mpcops.o: mpcops.hpp types.hpp mpcio.hpp coroutine.hpp bitutils.hpp
rdpf.o: rdpf.hpp mpcio.hpp types.hpp coroutine.hpp bitutils.hpp dpf.hpp
rdpf.o: prg.hpp aes.hpp rdpf.tcc mpcops.hpp
cdpf.o: bitutils.hpp cdpf.hpp mpcio.hpp types.hpp coroutine.hpp dpf.hpp
cdpf.o: prg.hpp aes.hpp cdpf.tcc

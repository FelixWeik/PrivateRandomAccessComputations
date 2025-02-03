all: prac

CXXFLAGS=-march=native -std=c++17 -Wall -Wno-ignored-attributes -ggdb -O0 -fsanitize=address -fno-omit-frame-pointer
LDFLAGS=-ggdb -fsanitize=address
LDLIBS=-lgmp -lgmpxx -lbsd -lboost_system -lboost_context -lboost_chrono -lboost_thread -lpthread

# Enable this to have all communication logged to stdout
# CXXFLAGS += -DVERBOSE_COMMS

BIN=prac
SRCS=prac.cpp mpcio.cpp preproc.cpp online.cpp mpcops.cpp rdpf.cpp duoram.cpp
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

prac.o: mpcio.hpp types.hpp bitutils.hpp corotypes.hpp mpcio.tcc preproc.hpp types.tcc duoram.tcc
prac.o: options.hpp online.hpp
mpcio.o: mpcio.hpp types.hpp bitutils.hpp corotypes.hpp mpcio.tcc rdpf.hpp types.tcc
mpcio.o: coroutine.hpp dpf.hpp prg.hpp aes.hpp rdpf.tcc mpcops.hpp mpcops.tcc
preproc.o: types.hpp bitutils.hpp coroutine.hpp corotypes.hpp mpcio.hpp types.tcc
preproc.o: mpcio.tcc preproc.hpp options.hpp rdpf.hpp dpf.hpp prg.hpp aes.hpp
online.o: online.hpp mpcio.hpp types.hpp bitutils.hpp corotypes.hpp mpcio.tcc types.tcc
online.o: options.hpp mpcops.hpp coroutine.hpp mpcops.tcc rdpf.hpp dpf.hpp
mpcops.o: mpcops.hpp types.hpp bitutils.hpp mpcio.hpp corotypes.hpp mpcio.tcc types.tcc
mpcops.o: coroutine.hpp mpcops.tcc
rdpf.o: rdpf.hpp mpcio.hpp types.hpp bitutils.hpp corotypes.hpp mpcio.tcc types.tcc
rdpf.o: coroutine.hpp dpf.hpp prg.hpp aes.hpp rdpf.tcc mpcops.hpp mpcops.tcc
duoram.o: duoram.hpp types.hpp bitutils.hpp mpcio.hpp corotypes.hpp mpcio.tcc types.tcc
duoram.o: coroutine.hpp rdpf.hpp dpf.hpp prg.hpp aes.hpp rdpf.tcc mpcops.hpp
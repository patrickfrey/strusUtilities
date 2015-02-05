TOPDIR=../..
include $(TOPDIR)/variables.mk

CPP_OBJS=

CPP_INCLUDE=-I$(TOPDIR)/include -I$(TOPDIR)/include/private -I$(INSTALL_INCDIR)
CXXFLAGS=-g -Werror -Wall -pedantic $(OPTIMIZE) -std=c++98
LDFLAGS=-L$(INSTALL_LIBDIR)
LIBS=-lstrus_database_leveldb -lstrus_storage -lstrus_queryeval -lstrus_queryproc -lstrus_analyzer -lstrus_tokenminer -lstrus_utils -lstemmer -lboost_system

all: strusQuery

-include $(CPP_OBJS:.o=.d)

%.o : %.cpp GNUmakefile
	$(CXX) -c -o $@ $(CXXFLAGS) $(CPP_INCLUDE) $*.cpp
	$(CXX) -MM $(CXXFLAGS) $(CPP_INCLUDE) $*.cpp > $*.d

strusQuery: $(CPP_OBJS) strusQuery.o
	$(CXX) -o $@ strusQuery.o $(CPP_OBJS) $(LDFLAGS) $(LIBS)

.PHONY: clean
clean:
	rm -f ${CPP_OBJS} ${CPP_OBJS:.o=.d} strusQuery strusQuery.o strusQuery.d

.PHONY: install
install:
	@mkdir -p $(INSTALL_BINDIR)
	@cp strusQuery $(INSTALL_BINDIR)

.PHONY: uninstall
uninstall:
	@rm $(INSTALL_BINDIR)/strusQuery




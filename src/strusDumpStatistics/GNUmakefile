TOPDIR=../..
include $(TOPDIR)/variables.mk

CPP_OBJS=

CPP_INCLUDE=-I$(TOPDIR)/include -I$(TOPDIR)/include/private -I$(INSTALL_INCDIR)
CXXFLAGS=-g -Werror -Wall -pedantic $(OPTIMIZE) -std=c++98
LDFLAGS=-L$(INSTALL_LIBDIR)
LIBS=-lstrus_database_leveldb -lstrus_storage -lstrus_utils -lboost_system

all: strusDumpStatistics

-include $(CPP_OBJS:.o=.d)

%.o : %.cpp GNUmakefile
	$(CXX) -c -o $@ $(CXXFLAGS) $(CPP_INCLUDE) $*.cpp
	$(CXX) -MM $(CXXFLAGS) $(CPP_INCLUDE) $*.cpp > $*.d

strusDumpStatistics: $(CPP_OBJS) strusDumpStatistics.o
	$(CXX) -o $@ strusDumpStatistics.o $(CPP_OBJS) $(LDFLAGS) $(LIBS)

.PHONY: clean
clean:
	rm -f ${CPP_OBJS} strusDumpStatistics strusDumpStatistics.o strusDumpStatistics.d

.PHONY: install
install:
	@mkdir -p $(INSTALL_BINDIR)
	@cp strusDumpStatistics $(INSTALL_BINDIR)

.PHONY: uninstall
uninstall:
	@rm $(INSTALL_BINDIR)/strusDumpStatistics




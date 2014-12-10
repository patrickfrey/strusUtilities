TOPDIR=../..
include $(TOPDIR)/variables.mk

CPP_OBJS= \
	fileCrawler.o \
	insertProcessor.o \
	checkInsertProcessor.o \
	commitQueue.o

CPP_INCLUDE=-I$(TOPDIR)/include -I$(TOPDIR)/include/private -I$(INSTALL_INCDIR)
CXXFLAGS=-g -Werror -Wall -pedantic $(OPTIMIZE) -std=c++98
LDFLAGS=-L$(INSTALL_LIBDIR)
LIBS=-lstrus_analyzer -lstrus_tokenminer -lstemmer -lstrus_utils -lstrus_lvdbstorage -lboost_system -lboost_thread

all: strusInsert strusCheckInsert

-include $(CPP_OBJS:.o=.d)

%.o : %.cpp GNUmakefile
	$(CXX) -c -o $@ $(CXXFLAGS) $(CPP_INCLUDE) $*.cpp
	$(CXX) -MM $(CXXFLAGS) $(CPP_INCLUDE) $*.cpp > $*.d

strusInsert: $(CPP_OBJS) strusInsert.o
	$(CXX) -o $@ strusInsert.o $(CPP_OBJS) $(LDFLAGS) $(LIBS)

strusCheckInsert: $(CPP_OBJS) strusCheckInsert.o
		$(CXX) -o $@ strusCheckInsert.o $(CPP_OBJS) $(LDFLAGS) $(LIBS)

.PHONY: clean
clean:
	rm -f ${CPP_OBJS} ${CPP_OBJS:.o=.d} \
		strusInsert strusInsert.o strusInsert.d \
		strusCheckInsert strusCheckInsert.o strusCheckInsert.d

.PHONY: install
install:
	@mkdir -p $(INSTALL_BINDIR)
	@cp strusInsert $(INSTALL_BINDIR)
	@cp strusCheckInsert $(INSTALL_BINDIR)

.PHONY: uninstall
uninstall:
	@rm $(INSTALL_BINDIR)/strusInsert
	@rm $(INSTALL_BINDIR)/strusCheckInsert




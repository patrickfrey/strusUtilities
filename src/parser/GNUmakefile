TOPDIR=../..
include $(TOPDIR)/variables.mk

CPP_OBJS=\
	lexems.o \
	programParser.o

CPP_INCLUDE=-I$(TOPDIR)/include -I$(TOPDIR)/include/tests -I$(TOPDIR)/include/private -I.
CXXFLAGS=-g -Werror -Wall -Wfatal-errors -pedantic $(OPTIMIZE) -std=c++98
LDFLAGS=-L/usr/lib
LIBS=

all: libstrus_parser.so

-include $(CPP_OBJS:.o=.d)

%.o : %.cpp GNUmakefile $(HFILES)
	$(CXX) -fPIC $(VISIBILITY) -c -o $@ $(CXXFLAGS) $(CPP_INCLUDE) $*.cpp
	$(CXX) -MM $(CXXFLAGS) $(CPP_INCLUDE) $*.cpp > $*.d

libstrus_parser.so: $(CPP_OBJS)
	$(CXX)  -shared -o $@ $(CPP_OBJS) $(LDFLAGS) $(LIBS)

.PHONY: clean
clean:
	rm -f $(CPP_OBJS) $(CPP_OBJS:.o=.d) libstrus_parser.so

.PHONY: install
install:
	@mkdir -p $(INSTALL_LIBDIR)
	@cp libstrus_parser.so $(INSTALL_LIBDIR)

.PHONY: uninstall
uninstall:
	@-rm -f $(INSTALL_LIBDIR)/libstrus_parser.so



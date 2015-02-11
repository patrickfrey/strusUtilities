TOPDIR=../..
include $(TOPDIR)/variables.mk

CPP_OBJS=

CPP_INCLUDE=-I$(TOPDIR)/include -I$(TOPDIR)/include/private -I$(INSTALL_INCDIR)
CXXFLAGS=-g -Werror -Wall -pedantic $(OPTIMIZE) -std=c++98
LDFLAGS=-L$(INSTALL_LIBDIR) -L$(TOPDIR)/src/program
LIBS=-lstrus_analyzer -lstrus_textprocessor -lstrus_segmenter_textwolf -lstrus_tokenizer_word -lstrus_tokenizer_punctuation -lstrus_normalizer_snowball -lstrus_utils -lstrus_program -lboost_system -lstemmer

all: strusAnalyze

-include $(CPP_OBJS:.o=.d)

%.o : %.cpp GNUmakefile
	$(CXX) -c -o $@ $(CXXFLAGS) $(CPP_INCLUDE) $*.cpp
	$(CXX) -MM $(CXXFLAGS) $(CPP_INCLUDE) $*.cpp > $*.d

strusAnalyze: $(CPP_OBJS) strusAnalyze.o
	$(CXX) -o $@ strusAnalyze.o $(CPP_OBJS) $(LDFLAGS) $(LIBS)

.PHONY: clean
clean:
	rm -f ${CPP_OBJS} ${CPP_OBJS:.o=.d} strusAnalyze strusAnalyze.o strusAnalyze.d


.PHONY: install
install:
	@mkdir -p $(INSTALL_BINDIR)
	@cp strusAnalyze $(INSTALL_BINDIR)

.PHONY: uninstall
uninstall:
	@rm $(INSTALL_BINDIR)/strusAnalyze




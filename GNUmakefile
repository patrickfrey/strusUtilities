include variables.mk

all:
	cd src; make all; cd ..

test: all

clean:
	cd src; make clean; cd ..

install:
	cd src; make install; cd ..
	@-mkdir -p $(INSTALL_INCDIR)
	@cp include/strus/*.hpp $(INSTALL_INCDIR)

uninstall:
	cd src; make uninstall; cd ..
	@-rm -f $(INSTALL_INCDIR)/strus/programLoader.hpp
	@-rm -f $(INSTALL_INCDIR)/strus/strus.hpp
	@-rmdir $(INSTALL_INCDIR)


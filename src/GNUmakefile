TOPDIR=..
include $(TOPDIR)/variables.mk

CPP_OBJS=

all: subdirs

.PHONY: subdirs
subdirs:
	cd system; make all; cd -
	cd strusAnalyze; make all; cd -
	cd strusInsert; make all; cd -
	cd strusQuery; make all; cd -
	cd strusCreate; make all; cd -
	cd strusInspect; make all; cd -

.PHONY: clean
clean:
	cd system; make clean; cd -
	cd strusAnalyze; make clean; cd -
	cd strusInsert; make clean; cd -
	cd strusQuery; make clean; cd -
	cd strusCreate; make clean; cd -
	cd strusInspect; make clean; cd -

.PHONY: install
install:
	cd system; make install; cd -
	cd strusAnalyze; make install; cd -
	cd strusInsert; make install; cd -
	cd strusQuery; make install; cd -
	cd strusCreate; make install; cd -
	cd strusInspect; make install; cd -

.PHONY: uninstall
uninstall:
	cd system; make uninstall; cd -
	cd strusAnalyze; make uninstall; cd -
	cd strusInsert; make uninstall; cd -
	cd strusQuery; make uninstall; cd -
	cd strusCreate; make uninstall; cd -
	cd strusInspect; make uninstall; cd -




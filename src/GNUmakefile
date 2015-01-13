TOPDIR=..
include $(TOPDIR)/variables.mk

CPP_OBJS=

all: subdirs

.PHONY: subdirs
subdirs:
	cd strusAnalyze; make all; cd -
	cd strusInsert; make all; cd -
	cd strusQuery; make all; cd -
	cd strusCreate; make all; cd -
	cd strusDestroy; make all; cd -
	cd strusInspect; make all; cd -
	cd strusAlterMetaData; make all; cd -

.PHONY: clean
clean:
	cd strusAnalyze; make clean; cd -
	cd strusInsert; make clean; cd -
	cd strusQuery; make clean; cd -
	cd strusCreate; make clean; cd -
	cd strusDestroy; make clean; cd -
	cd strusInspect; make clean; cd -
	cd strusAlterMetaData; make clean; cd -

.PHONY: install
install:
	cd strusAnalyze; make install; cd -
	cd strusInsert; make install; cd -
	cd strusQuery; make install; cd -
	cd strusCreate; make install; cd -
	cd strusDestroy; make install; cd -
	cd strusInspect; make install; cd -
	cd strusAlterMetaData; make install; cd -

.PHONY: uninstall
uninstall:
	cd strusAnalyze; make uninstall; cd -
	cd strusInsert; make uninstall; cd -
	cd strusQuery; make uninstall; cd -
	cd strusCreate; make uninstall; cd -
	cd strusDestroy; make uninstall; cd -
	cd strusInspect; make uninstall; cd -
	cd strusAlterMetaData; make uninstall; cd -




MOC_CPP_SRCS = qt-main.cpp
CPP_SRCS = processing.cpp interactive-processor.cpp
HEADERS = processing.hpp interactive-processor.hpp vec.hpp
DOCFILES = LICENSE

PROG = photoproc

prefix ?= /usr
bindir ?= $(prefix)/bin
datadir ?= $(prefix)/share

CFLAGS += -O3 -fomit-frame-pointer
CFLAGS += -Wall
CFLAGS += -D_GNU_SOURCE -D_THREAD_SAFE -enable-threads

CFLAGS += -I$(QTDIR)/include -I$(QTDIR)/mkspecs/default -I/usr/include/freetype2
CFLAGS += -D_REENTRANT -DQT_NO_DEBUG -DQT_THREAD_SUPPORT
LDADD += -Wl,-rpath,$(QTDIR)/lib -L$(QTDIR)/lib -L/usr/X11R6/lib -lpthread -lXext -lX11 -lm -lqt-mt
# for qt static linking: LDADD += /usr/lib/qt-3.0.5/lib/libqt-mt.a -lGL -lXft -lSM -lXinerama -lmng -lpng

CPP=g++
LD=gcc
MOC=$(QTDIR)/bin/moc

.SUFFIXES: .moc .o

.cpp.o:
	$(CPP) -c $(CFLAGS) `Magick++-config --cxxflags --cppflags` $<
.cpp.moc:
	$(MOC) $< -o $@ 

MOCS = $(MOC_CPP_SRCS:%.cpp=%.moc)
OBJS = $(CPP_SRCS:%.cpp=%.o) $(MOC_CPP_SRCS:%.cpp=%.o)

all: $(PROG)

$(PROG): $(MOCS) $(OBJS)
	$(LD) $(LDFLAGS) `Magick++-config --ldflags --libs` -o $@ $(OBJS) $(LDADD)

clean:
	@rm -rf *.o *.so *.a *.moc $(PROG)

RELEASE_SOURCES = $(MOC_CPP_SRCS) $(CPP_SRCS) $(HEADERS) Makefile $(DOCFILES)
RELASE_NAME = $(PROG)-$(VER)

install release: VER = $(shell grep PHOTOPROC_VERSION $(MOC_CPP_SRCS) | head -1 | cut '-d"' -f2)

install: $(PROG) $(DOCFILES)
	mkdir -p $(bindir) $(datadir)/doc/$(RELASE_NAME)
	install -s -m 755 $(PROG) $(bindir)
	install -s -m 644 $(DOCFILES) $(datadir)/doc/$(RELASE_NAME)

release: $(RELEASE_SOURCES)
	@echo
	@echo Creating release $(RELASE_NAME)
	@echo
	rm -rf $(RELASE_NAME) $(RELASE_NAME).zip $(RELASE_NAME).tar.gz $(RELASE_NAME).spec $(RELASE_NAME)*.rpm
	mkdir $(RELASE_NAME)
	cp $(RELEASE_SOURCES) $(RELASE_NAME)
	tar -czf $(RELASE_NAME).tar.gz $(RELASE_NAME)
	zip -q $(RELASE_NAME).zip $(RELEASE_SOURCES)
	rm -rf $(RELASE_NAME)
	cp $(RELASE_NAME).tar.gz rpmbuild-sources/
	echo Version: $(VER) > $(RELASE_NAME).spec
	cat specfile >> $(RELASE_NAME).spec
	rpmbuild --clean --rmspec --rmsource -bs $(RELASE_NAME).spec
	mv rpmbuild-srpms/$(RELASE_NAME)-1.src.rpm ./$(RELASE_NAME).src.rpm
	rpmbuild --recompile $(RELASE_NAME).src.rpm
	@echo
	@echo Release files created:
	@echo "      " $(RELASE_NAME).zip
	@echo "      " $(RELASE_NAME).tar.gz
	@echo "      " $(RELASE_NAME).src.rpm

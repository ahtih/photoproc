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
LD=g++
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

STATIC_LIBFILES=/usr/lib/libMagick++.a \
		/usr/lib/libMagick.a \
		/usr/X11R6/lib/libXft.a \
		/usr/X11R6/lib/libXinerama.a \
		/usr/X11R6/lib/libXrandr.a \
		/usr/X11R6/lib/libXcursor.a \
		/usr/X11R6/lib/libXrender.a \
		/usr/lib/libfontconfig.a \
		/usr/lib/libexpat.a \
		/usr/lib/libxml2.a \
		/usr/lib/libfreetype.a \
		/usr/lib/libbz2.a \
		/usr/lib/libz.a \
		/usr/lib/libtiff.a
STATIC_QTDIR ?= $(QTDIR)

$(PROG)-static: GCCLIB_DIR=$(shell dirname `$(CPP) --print-libgcc-file-name`)
$(PROG)-static: QTDIR=$(STATIC_QTDIR)

$(PROG)-static: $(STATIC_QTDIR)/lib/libqt-mt.a $(STATIC_LIBFILES) $(MOCS) $(OBJS)
# half-static, but does not run:  $(LD) -o $@ $(OBJS) -lICE -lSM -lX11 -lXext -lpthread -static-libgcc $(QTDIR)/lib/libqt-mt.a -ldl -Wl,-static,-lXft,-lfontconfig,-lexpat,-lxml2,-lXinerama,-lXrandr,-lXcursor,-lXrender,-lfreetype,-lz `Magick++-config --ldflags --libs`
# completely static, but crashes: $(LD) -static `Magick++-config --ldflags` -o $@ $(OBJS) $(QTDIR)/lib/libqt-mt.a -pthread -lMagick++ -lMagick -lXft -lX11 -lXext -lGL -lSM -lXinerama -lpng -lXcursor -lXrandr -ltiff -lfontconfig -lICE -lXrender -lfreetype -lexpat -lxml2 -lbz2 -lz -ldl
	$(LD) -o $@ $(OBJS) -nodefaultlibs -L/usr/X11R6/lib \
		-lpthread -lXext -lX11 -lm -lc \
		$(QTDIR)/lib/libqt-mt.a $(STATIC_LIBFILES) -lICE -lSM \
		$(GCCLIB_DIR)/libgcc.a \
		$(GCCLIB_DIR)/libgcc_eh.a \
		$(GCCLIB_DIR)/libstdc++.a
	@forbiddenlibs=`ldd $@ | grep -Ev 'libpthread.so|libXext.so|libX11.so|libm.so|libc.so|libICE.so|libSM.so|libdl.so|ld-linux.so'`; \
	if [ "$$forbiddenlibs" ] ; then \
		rm -f $@ ; echo ; echo ; \
		echo "Error: $(PROG)-static uses the following forbidden shared libs:"; \
		echo $$forbiddenlibs; echo ; exit 1 ; \
	fi;

clean:
	@rm -rf *.o *.so *.a *.moc $(PROG) $(PROG)-static

RELEASE_SOURCES = $(MOC_CPP_SRCS) $(CPP_SRCS) $(HEADERS) Makefile $(DOCFILES)
RELASE_NAME = $(PROG)-$(VER)

install release: VER = $(shell grep PHOTOPROC_VERSION $(MOC_CPP_SRCS) | head -1 | cut '-d"' -f2)

PROGREQ?=$(PROG)

install: $(DOCFILES) $(PROGREQ)
	mkdir -p $(bindir) $(datadir)/doc/$(RELASE_NAME)
	install -m 755 -s $(PROG) $(bindir)
	install -m 644 $(DOCFILES) $(datadir)/doc/$(RELASE_NAME)

print-creating-release:
	@echo
	@echo Creating release $(RELASE_NAME)
	@echo

release: print-creating-release $(RELEASE_SOURCES) $(PROG)-static
	rm -rf $(RELASE_NAME) $(RELASE_NAME).zip $(RELASE_NAME).tar.gz $(RELASE_NAME).spec $(RELASE_NAME)*.rpm $(RELASE_NAME)-linux-i386-static-binary.gz
	#
	# Make source .tar.gz and .zip
	#
	mkdir $(RELASE_NAME)
	cp $(RELEASE_SOURCES) $(RELASE_NAME)
	tar -czf $(RELASE_NAME).tar.gz $(RELASE_NAME)
	zip -q $(RELASE_NAME).zip $(RELEASE_SOURCES)
	rm -rf $(RELASE_NAME)
	#
	# Make Linux static binary .gz
	#
	cat $(PROG)-static | gzip -cn > $(RELASE_NAME)-linux-i386-static-binary.gz
	#
	# Make SRPM
	#
	cp $(RELASE_NAME).tar.gz rpmbuild-sources/
	echo Version: $(VER) > $(RELASE_NAME).spec
	cat specfile >> $(RELASE_NAME).spec
	rpmbuild --clean --rmspec --rmsource -bs $(RELASE_NAME).spec
	mv rpmbuild-srpms/$(RELASE_NAME)-1.src.rpm ./$(RELASE_NAME).src.rpm
	#
	# Make Linux static RPM
	#
	tar -czf rpmbuild-sources/$(RELASE_NAME)-static.tar.gz $(PROG)-static $(DOCFILES) Makefile $(MOC_CPP_SRCS)
	echo Version: $(VER) > $(RELASE_NAME).spec
	cat specfile-static >> $(RELASE_NAME).spec
	rpmbuild --clean --rmspec --rmsource -bb $(RELASE_NAME).spec
	mv rpmbuild-rpms/$(RELASE_NAME)-1.i386.rpm ./$(RELASE_NAME)-static.i386.rpm
	#
	# Do a test build from SRPM
	#
	rpmbuild --recompile $(RELASE_NAME).src.rpm
	@echo
	@echo Release files created:
	@echo "      " $(RELASE_NAME).zip
	@echo "      " $(RELASE_NAME).tar.gz
	@echo "      " $(RELASE_NAME).src.rpm
	@echo "      " $(RELASE_NAME)-linux-i386-static-binary.gz
	@echo "      " $(RELASE_NAME)-static.i386.rpm

###########################################################################
# Release procedure:
#
# 1. Update the PHOTOPROC_VERSION #define in qt-main.cpp
# 2. Do "make relnotes" and update the ChangeLog file accordingly
# 3. Do "make release"
# 4. Upload the release files to Sourceforge
# 5. Do "cvs tag -c release-X-XX" to tag the CVS repository
###########################################################################

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

.PHONY: all clean realclean install release relnotes print-creating-release

all: $(PROG)

$(PROG): $(MOCS) $(OBJS)
	$(LD) $(LDFLAGS) `Magick++-config --ldflags --libs` -o $@ $(OBJS) $(LDADD)

# The idea of photoproc static linking is somewhat limited, because
# ImageMagick does not lend itself well to static linking -- it requires
# its configuration files to be present in filesystem, thus ImageMagick
# pretty much needs to be installed anyway to use photoproc. As a result, Qt
# (and things it requires) is the only component that we can link statically.
#
# We look for static Qt in $STATIC_QTDIR. Qt for photoproc static linking
# can be built with: (build requires 550Mb disk space, install requires 75Mb)
#
# QTDIR=`pwd` ./configure -no-xrandr -no-xcursor -no-ipv6 -no-cups -no-nis
#			-no-xrender -no-xft -no-xinerama -no-sm
#			-no-nas-sound -system-libjpeg
#			-no-imgfmt-mng -disable-opengl -disable-network
#			-disable-sql -system-zlib
#			-static -thread -prefix $STATIC_QTDIR
# QTDIR=`pwd` SUBLIBS="-lpng -ljpeg" make

STATIC_QTDIR ?= $(QTDIR)

$(PROG)-static: GCCLIB_DIR=$(shell dirname `$(CPP) --print-libgcc-file-name`)
$(PROG)-static: QTDIR=$(STATIC_QTDIR)

$(PROG)-static: $(STATIC_QTDIR)/lib/libqt-mt.a $(MOCS) $(OBJS)
	$(LD) -o $@ $(OBJS) -L/usr/X11R6/lib \
		`Magick++-config --ldflags --libs` \
		$(QTDIR)/lib/libqt-mt.a \
		-lpthread -lXext -lX11 -lm -lc -lc_nonshared -lpng

clean:
	@rm -rf *.o *.so *.a *.moc $(PROG) $(PROG)-static

RELEASE_SOURCES = $(MOC_CPP_SRCS) $(CPP_SRCS) $(HEADERS) Makefile $(DOCFILES)
RELASE_NAME = $(PROG)-$(VER)

install release realclean: VER = $(shell grep PHOTOPROC_VERSION $(MOC_CPP_SRCS) | head -1 | cut '-d"' -f2)

PROGREQ?=$(PROG)

install: $(DOCFILES) $(PROGREQ)
	mkdir -p $(bindir) $(datadir)/doc/$(RELASE_NAME)
	install -m 755 -s $(PROG) $(bindir)
	install -m 644 $(DOCFILES) $(datadir)/doc/$(RELASE_NAME)

realclean: clean
	rm -rf $(RELASE_NAME) $(RELASE_NAME).zip $(RELASE_NAME).tar.gz $(RELASE_NAME).spec $(RELASE_NAME)*.rpm $(RELASE_NAME)-linux-i386-static-binary.gz

relnotes: $(MOC_CPP_SRCS)
	@cvs log -SN `cvs status -v $(MOC_CPP_SRCS) | grep 'revision: ' | \
		head -1 | sed -e 's/^.*release-/-rrelease-/' -e 's/ .*/::/'` | \
		egrep -v '^RCS file:|^head:|^branch:|^locks:|^access list:|^keyword substitution:|^total revisions:|^description:|^----------------------------' | \
		sed -e 's/^revision [0-9].*//' -e 's/^date: .*author:.*state:.*//'

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

# ImageMagick for armv5tel-cacko-linux is configured with:
#
# ./configure LDFLAGS=-L/opt/cross/arm/3.4.4-xscale-softvfp/armv5tel-cacko-linux/lib
#   CXXFLAGS=-fno-rtti
#   CC=/opt/cross/arm/3.4.4-xscale-softvfp/bin/armv5tel-linux-gcc
#   CXX=/opt/cross/arm/3.4.4-xscale-softvfp/bin/armv5tel-linux-g++
#   --disable-shared
#   --disable-largefile --without-threads --without-perl --without-bzlib
#   --without-dps  --without-gvc --without-jbig --without-jbig --without-jpeg
#   --without-jp2 --without-lcms --without-png --without-tiff --without-ttf
#   --without-wmf --without-xml --without-zlib --without-x
#   --with-quantum-depth=8
#   --host=armv5tel-cacko-linux --build=i686-host-linux-gnu
#   --target=armv5tel-cacko-linux
#   --prefix=/opt/cross/arm/3.4.4-xscale-softvfp/ImageMagick
#   --program-prefix=

# QTDIR=/opt/cross/arm/3.4.4-xscale-softvfp/armv5tel-cacko-linux/qt
# CPP=/opt/cross/arm/3.4.4-xscale-softvfp/bin/armv5tel-linux-g++
# CFLAGS += -DPHOTOPROC_ALWAYS_USE_HALFRES
# MAGICKCPP_CONFIG_PREFIX = PATH=/opt/cross/arm/3.4.4-xscale-softvfp/ImageMagick/bin:$$PATH 

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
CPP_SRCS = processing.cpp interactive-processor.cpp color-patches-detector.cpp
HEADERS = processing.hpp interactive-processor.hpp color-patches-detector.hpp vec.hpp
DOCFILES = LICENSE

PROG = photoproc

prefix ?= /usr
bindir ?= $(prefix)/bin
datadir ?= $(prefix)/share

CFLAGS += -O3 -fomit-frame-pointer -fno-rtti
CFLAGS += -Wall -Wunused-parameter
CFLAGS += -D_GNU_SOURCE -D_THREAD_SAFE -enable-threads

CFLAGS += -I$(QTDIR)/include -I$(QTDIR)/mkspecs/default -I/usr/include/freetype2
CFLAGS += -D_REENTRANT -DQT_NO_DEBUG -DQT_THREAD_SUPPORT
LDADD += -L/usr/X11R6/lib
LDADD += -Wl,-rpath,$(QTDIR)/lib -L$(QTDIR)/lib -lpthread -lXext -lX11 -lm -lqt-mt

CPP=g++
LD=$(CPP)
MOC=$(QTDIR)/bin/moc

.SUFFIXES: .moc .o

.cpp.o:
	$(CPP) -c $(CFLAGS) `$(MAGICKCPP_CONFIG_PREFIX)Magick++-config --cxxflags --cppflags` $<
.cpp.moc:
	$(MOC) $< -o $@ 

MOCS = $(MOC_CPP_SRCS:%.cpp=%.moc)
OBJS = $(CPP_SRCS:%.cpp=%.o) $(MOC_CPP_SRCS:%.cpp=%.o)

.PHONY: all clean realclean install release relnotes print-creating-release

all: $(PROG)

$(PROG): $(MOCS) $(OBJS)
	$(LD) $(LDFLAGS) -o $@ $(OBJS) `$(MAGICKCPP_CONFIG_PREFIX)Magick++-config --ldflags --libs` $(LDADD)

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
		`$(MAGICKCPP_CONFIG_PREFIX)Magick++-config --ldflags --libs` \
		$(QTDIR)/lib/libqt-mt.a \
		-lpthread -lXext -lX11 -lm -lc -lc_nonshared -lpng

clean:
	@rm -rf *.o *.so *.a *.moc $(PROG) $(PROG)-static

RELEASE_SOURCES = $(MOC_CPP_SRCS) $(CPP_SRCS) $(HEADERS) Makefile $(DOCFILES)
RELEASE_NAME = $(PROG)-$(VER)

install release realclean: VER = $(shell grep PHOTOPROC_VERSION $(MOC_CPP_SRCS) | head -1 | cut '-d"' -f2)

PROGREQ?=$(PROG)

install: $(DOCFILES) $(PROGREQ)
	mkdir -p $(bindir) $(datadir)/doc/$(RELEASE_NAME)
	install -m 755 -s $(PROG) $(bindir)
	install -m 644 $(DOCFILES) $(datadir)/doc/$(RELEASE_NAME)

realclean: clean
	rm -rf $(RELEASE_NAME) $(RELEASE_NAME).zip $(RELEASE_NAME).tar.gz $(RELEASE_NAME).spec $(RELEASE_NAME)*.rpm $(RELEASE_NAME)-linux-i386-static-binary.gz

relnotes: $(MOC_CPP_SRCS)
	@cvs log -SN `cvs status -v $(MOC_CPP_SRCS) | grep 'revision: ' | \
		head -1 | sed -e 's/^.*release-/-rrelease-/' -e 's/ .*/::/'` | \
		egrep -v '^RCS file:|^head:|^branch:|^locks:|^access list:|^keyword substitution:|^total revisions:|^description:|^----------------------------' | \
		sed -e 's/^revision [0-9].*//' -e 's/^date: .*author:.*state:.*//'

print-creating-release:
	@echo
	@echo Creating release $(RELEASE_NAME)
	@echo

release: print-creating-release $(RELEASE_SOURCES) $(PROG)-static
	rm -rf $(RELEASE_NAME) $(RELEASE_NAME).zip $(RELEASE_NAME).tar.gz $(RELEASE_NAME).spec $(RELEASE_NAME)*.rpm $(RELEASE_NAME)-linux-i386-static-binary.gz
	#
	# Make source .tar.gz and .zip
	#
	mkdir $(RELEASE_NAME)
	cp $(RELEASE_SOURCES) $(RELEASE_NAME)
	tar -czf $(RELEASE_NAME).tar.gz $(RELEASE_NAME)
	zip -q $(RELEASE_NAME).zip $(RELEASE_SOURCES)
	rm -rf $(RELEASE_NAME)
	#
	# Make Linux static binary .gz
	#
	cat $(PROG)-static | gzip -cn > $(RELEASE_NAME)-linux-i386-static-binary.gz
	#
	# Make SRPM
	#
	cp $(RELEASE_NAME).tar.gz rpmbuild-sources/
	echo Version: $(VER) > $(RELEASE_NAME).spec
	cat specfile >> $(RELEASE_NAME).spec
	rpmbuild --clean --rmspec --rmsource -bs $(RELEASE_NAME).spec
	mv rpmbuild-srpms/$(RELEASE_NAME)-1.src.rpm ./$(RELEASE_NAME).src.rpm
	#
	# Make Linux static RPM
	#
	tar -czf rpmbuild-sources/$(RELEASE_NAME)-static.tar.gz $(PROG)-static $(DOCFILES) Makefile $(MOC_CPP_SRCS)
	echo Version: $(VER) > $(RELEASE_NAME).spec
	cat specfile-static >> $(RELEASE_NAME).spec
	rpmbuild --clean --rmspec --rmsource -bb $(RELEASE_NAME).spec
	mv rpmbuild-rpms/$(RELEASE_NAME)-1.i386.rpm ./$(RELEASE_NAME)-static.i386.rpm
	#
	# Do a test build from SRPM
	#
	rpmbuild --recompile $(RELEASE_NAME).src.rpm
	@echo
	@echo Release files created:
	@echo "      " $(RELEASE_NAME).zip
	@echo "      " $(RELEASE_NAME).tar.gz
	@echo "      " $(RELEASE_NAME).src.rpm
	@echo "      " $(RELEASE_NAME)-linux-i386-static-binary.gz
	@echo "      " $(RELEASE_NAME)-static.i386.rpm

MOC_CPP_SRCS = qt-main.cpp
CPP_SRCS = processing.cpp interactive-processor.cpp

PROG = photoproc

CFLAGS += -O2
CFLAGS += -Wall -fno-for-scope
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

all:: $(PROG)

$(PROG): $(MOCS) $(OBJS)
	$(LD) $(LDFLAGS) `Magick++-config --ldflags --libs` -o $@ $(OBJS) $(LDADD)

clean::
	@rm -f *.o *.so *.a *.moc $(PROG)

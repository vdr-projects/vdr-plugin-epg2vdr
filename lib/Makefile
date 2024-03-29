#
# Makefile
#
# See the README file for copyright information and how to reach the author.
#

include ../Make.config

LIBTARGET = libhorchi
HLIB      = -L. -lhorchi

DEMO = demo
TEST = tst

LIBOBJS = common.o config.o db.o epgservice.o dbdict.o json.o xml.o

ifdef USEJPEG
   LIBOBJS += imgtools.o
endif

ifdef USECURL
   LIBOBJS += curl.o configuration.o thread.o
endif

CFLAGS += $(shell pkg-config --cflags uuid)
CFLAGS += $(shell pkg-config --cflags zlib)

BASELIBS += -lrt
BASELIBS += $(shell $(SQLCFG) --libs_r)
BASELIBS += $(shell pkg-config --libs uuid)
BASELIBS += $(shell pkg-config --libs zlib)
BASELIBS += $(PYTHON_LIBS)
BASELIBS += $(shell pkg-config --libs tinyxml2)

ifdef USECURL
   BASELIBS += -lcurl
endif

ifdef USEEPGS
   LIBOBJS += searchtimer.o
endif

ifdef USEPYTHON
   LIBOBJS += python.o
endif

ifdef USELIBXML
   BASELIBS += $(shell xml2-config --libs) $(shell xslt-config --libs)
endif

ifdef SYSD_NOTIFY
   BASELIBS  += $(shell pkg-config --libs libsystemd-daemon)
   CFLAGS += $(shell pkg-config --cflags libsystemd-daemon)
endif

CFLAGS += $(shell $(SQLCFG) --include)
DEFINES += $(USES)

all: lib $(TEST) $(DEMO)
lib: $(LIBTARGET).a

$(LIBTARGET).a : $(LIBOBJS)
	@echo Building Lib ...
	$(doLib) $@ $(LIBOBJS)

tst: test.o lib
	$(doLink) test.o $(HLIB) -larchive -lcrypto $(BASELIBS) -o $@

demo: demo.o lib
	$(doLink) demo.o $(HLIB) -larchive -lcrypto $(BASELIBS) -o $@

pytst: pytst.c python.c python.h hlib
	$(CC) $(CFLAGS) pytst.c python.c -L./lib -lhorchi $(DLIBS) -o pytst

clean:
	rm -f *.o *~ core $(TEST) $(DEMO) $(LIBTARGET).a

cppchk:
	cppcheck --template="{file}:{line}:{severity}:{message}" --quiet --force *.c *.h

%.o: %.c
	@echo Compile "$(*F)" ...
	$(doCompile) -O3 $(INCLUDES) -o $@ $<

#--------------------------------------------------------
# dependencies
#--------------------------------------------------------

HEADER = db.h common.h config.h dbdict.h

common.o          :  common.c           $(HEADER) common.h
configuration.o   :  configuration.c    $(HEADER) configuration.h
thread.o     		:  thread.c      		 $(HEADER) thread.h
curl.o       		:  curl.c        		 $(HEADER) curl.h
imgtools.o   		:  imgtools.c    		 $(HEADER) imgtools.h
config.o     		:  config.c      		 $(HEADER) config.h
db.o         		:  db.c          		 $(HEADER) db.h
epgservice.o 		:  epgservice.c  		 $(HEADER) epgservice.h
dbdict.o     		:  dbdict.c      		 $(HEADER) dbdict.h
json.o       		:  json.c        		 $(HEADER) json.h
xml.o       		:  xml.c        		 $(HEADER) xml.h
python.o          :  python.c           $(HEADER) python.h
searchtimer.o     :  searchtimer.c      $(HEADER) searchtimer.h

demo.o       		:  demo.c        		 $(HEADER)
test.o       		:  test.c        		 $(HEADER)

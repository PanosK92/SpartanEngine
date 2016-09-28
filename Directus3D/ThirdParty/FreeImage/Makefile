# Entry point for FreeImage makefiles
# Default to 'make -f Makefile.gnu' for Linux and for unknown OS. 
#
OS = $(shell uname)
MAKEFILE = gnu

ifeq ($(OS), Darwin)
    MAKEFILE = osx
endif
ifeq ($(OS), Cygwin)
    MAKEFILE = cygwin
endif
ifeq ($(OS), Solaris)
    MAKEFILE = solaris
endif
ifeq ($(OS), windows32)
    MAKEFILE = mingw
endif

default:
	$(MAKE) -f Makefile.$(MAKEFILE) 

all:
	$(MAKE) -f Makefile.$(MAKEFILE) all 

dist:
	$(MAKE) -f Makefile.$(MAKEFILE) dist 

install:
	$(MAKE) -f Makefile.$(MAKEFILE) install 

clean:
	$(MAKE) -f Makefile.$(MAKEFILE) clean 


### Some settings one might reasonably want to change

# set  this if you want to see SPI transactions in gory detail 
SPI_DEBUG=0

# set this to "cheat" on reading thresholds (just copies back last set thresholds instead of actually reading them) 
#    This will avoid having to implement reading thresholds in firmware, but will return bogus values if
#    you read before setting. Or if the thresholds get set in a different way... 
CHEAT_READ_THRESHOLDS=0




CC=gcc
LD=gcc

#I'm lazy and using implicit rules for now, which means everything gets the same cflags
CFLAGS+=-fPIC -g -Wall -Wextra  -D_GNU_SOURCE -O2 -std=gnu17 -D_BEACON_ #-DBENCHMARK
LDFLAGS+= -lz -g

DAQ_LDFLAGS+= -lpthread  -L./ -g 


ifeq ($(SPI_DEBUG),1)
CFLAGS+=-DDEBUG_PRINTOUTS
endif

ifeq ($(CHEAT_READ_THRESHOLDS),1)
	CFLAGS+=-DCHEAT_READ_THRESHOLDS
endif


PREFIX=/beacon
LIBDIR=lib 
INCLUDEDIR=include

.PHONY: clean install doc install-doc all client



HEADERS = beacon.h 
OBJS = beacon.o 

DAQ_HEADERS =flower8.h
DAQ_OBJS =  flower8.o

all: libbeacon.so libflower8.so 

client: libbeacon.so 

libbeacon.so: $(OBJS) $(HEADERS)
	$(CC) $(LDFLAGS)  -shared $(OBJS) -o $@

libflower8.so: $(DAQ_OBJS) $(DAQ_HEADERS) 
	$(CC) $(LDFLAGS) $(DAQ_LDFLAGS) -shared $(DAQ_OBJS) -o $@ 

install-doc:
	install -d $(PREFIX)/$(SHARE) 
	install doc $(PREFIX)/share 

install-client:  client 
	install -d $(PREFIX)/$(LIBDIR)
	install -d $(PREFIX)/$(INCLUDEDIR)
	install libbeacon.so $(PREFIX)/$(LIBDIR)  
	install $(HEADERS) $(PREFIX)/$(INCLUDEDIR)
	-echo $(PREFIX)/$(LIBDIR) >> /etc/ld.so.conf.d/beacon.conf
	-ldconfig
	
install:  all install-client 
	install $(DAQ_HEADERS) $(PREFIX)/$(INCLUDEDIR) 
	install libflower8.so $(PREFIX)/$(LIBDIR)


doc: 
	doxygen doc/Doxyfile 

beacon.pdf: doc 
	make -C doc/latex  && cp doc/latex/refman.pdf $@ 

clean: 
	rm -f *.o *.so 
	rm -rf doc/latex
	rm -rf doc/html
	rm -rf doc/man

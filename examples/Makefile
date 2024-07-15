CC=gcc 
CFLAGS+=-g  -I../ -Wall -Wextra -D_GNU_SOURCE
LDFLAGS+= -L../ -lflower8 -lbeacon -lpthread -lm -lz -lcurl


EXAMPLES= dump_events dump_headers dump_status flower8-daqtest flower8-equalize flower8-threshscan flower8-set-coinc flower8-set-phased flower8-threshscan-phased

all: $(EXAMPLES) 

clean: 
	rm -f $(EXAMPLES) 




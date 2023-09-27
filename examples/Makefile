CC=gcc 
CFLAGS+=-g  -I../ -Wall -Wextra -D_GNU_SOURCE
LDFLAGS+= -L../ -lflower8 -lbeacon -lpthread -lm -lz -lcurl


EXAMPLES= dump_events dump_headers dump_status flower8-daqtest flower8-equalize flower8-threshscan

all: $(EXAMPLES) 

clean: 
	rm -f $(EXAMPLES) 




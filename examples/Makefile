CC=gcc 
CFLAGS+=-g  -I../ -Wall -Wextra -D_GNU_SOURCE
LDFLAGS+= -L../ -lbeacondaq -lbeacon -lpthread -lm -lz -lcurl


EXAMPLES= dump_events dump_headers read_ain \
				 dump_hk dump_status dump_shared_hk test_mate3

all: $(EXAMPLES) 

clean: 
	rm -f $(EXAMPLES) 




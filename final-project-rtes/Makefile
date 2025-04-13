INCLUDE_DIRS = 
LIB_DIRS = 
CC=gcc

CDEFS=
CFLAGS= -O0 -g $(INCLUDE_DIRS) $(CDEFS)
LIBS= -lrt -lpthread

HFILES= 
CFILES= q3-pgm.c q3-ppm.c q4.c q5-a.c q5-c.c

SRCS= ${HFILES} ${CFILES}
OBJS= ${CFILES:.c=.o}

all:    q3-pgm q3-ppm q4 q5-a q5-c	

clean:
	-rm -f *.o *.d
	-rm -f q3-ppm q3-pgm q4 q5-a q5-c

distclean:
	-rm -f *.o *.d

q3-ppm: q3-ppm.o
	$(CC) $(LDFLAGS) $(CFLAGS) -o $@ q3-ppm.o $(LIBS)

q3-pgm: q3-pgm.o
	$(CC) $(LDFLAGS) $(CFLAGS) -o $@ q3-pgm.o $(LIBS)

q4: q4.o
	$(CC) $(LDFLAGS) $(CFLAGS) -o $@ q4.o $(LIBS)

q5-a: q5-a.o
	$(CC) $(LDFLAGS) $(CFLAGS) -o $@ q5-a.o $(LIBS)

q5-c: q5-c.o
	$(CC) $(LDFLAGS) $(CFLAGS) -o $@ q5-c.o $(LIBS)

depend:

.c.o:
	$(CC) $(CFLAGS) -c $<

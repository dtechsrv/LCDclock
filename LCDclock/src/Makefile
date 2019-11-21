# build lcdclock executable when user executes "make"

lcdclock: lcdclock.o
	$(CC) $(LDFLAGS) lcdclock.o -o lcdclock

lcdclock.o: lcdclock.c
	$(CC) $(CFLAGS) -c lcdclock.c

# remove object files and executable when user executes "make clean"
clean:
	rm *.o lcdclock
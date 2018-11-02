GCC=/usr/bin/gcc
tools:tools.o
	$(GCC) tools.o -o tools

tools.o:tools.c tools.h
	$(GCC) -c tools.c

clean:
	rm -f *.o 
	rm -f tools
	rm -f test

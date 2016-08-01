all: horsepill_install Makefile

horsepill_install: horsepill_install.c infect.h infect.c banner.h
	gcc -o horsepill_install horsepill_install.c infect.c

clean: Makefile
	rm -v horsepill_install

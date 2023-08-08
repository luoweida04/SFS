 
all: init_disk SFS
init_disk: init_disk.o
	gcc init_disk.o -o init_disk
SFS: SFS.o
	gcc SFS.o -o SFS -Wall -D_FILE_OFFSET_BITS=64 -g -pthread -lfuse3 -lrt -ldl
SFS.o: SFS.c
	gcc -Wall -D_FILE_OFFSET_BITS=64 -g -c -o SFS.o SFS.c
init_disk.o: init_disk.c
	gcc -Wall -D_FILE_OFFSET_BITS=64 -g -c -o init_disk.o init_disk.c
.PHONY : all
clean :
	rm -f SFS init_disk SFS.o init_disk.o

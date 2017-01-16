CC = gcc

KERNELDIR = /usr/src/linux-2.4.18-14custom
include $(KERNELDIR)/.config

FLAGS = -D__KERNEL__ -DMODULE -I$(KERNELDIR)/include -O -Wall
OBJ = mastermind.o
FILES = mastermind.c mastermind.h hw4.h encryptor.h

$(OBJ) : $(FILES)
	$(CC) -c $(FLAGS) $(FILES)

clear :
	rm -f $(OBJ)
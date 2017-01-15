CC = gcc

KERNELDIR = /usr/src/linux-2.4.18-14custom
include $(KERNELDIR)/.config

FLAGS = -D__KERNEL__ -DMODULE -I$(KERNELDIR)/include -O -Wall
OBJ = simple_module.o
FILES = simple_module.c simple_module.h

simple_module.o : $(FILES)
	$(CC) -c $(FLAGS) $(FILES)

clear_objects :
	rm -f $(OBJ)
obj-m := assoofs.o

MKASSOOFS = mkassoofs

CC = gcc

CFLAGS = -Wall

all: ko $(MKASSOOFS)

ko:
	make -C /lib/modules/$(shell uname -r)/build M=$(shell pwd) modules

$(MKASSOOFS): mkassoofs.c assoofs.h
	$(CC) $(CFLAGS) -o $(MKASSOOFS) mkassoofs.c

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(shell pwd) clean
	rm -f $(MKASSOOFS)


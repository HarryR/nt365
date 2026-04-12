CC = gcc
AS = gcc
LD = ld

CFLAGS = -m32 -ffreestanding -nostdlib -nostartfiles -fno-builtin \
         -fno-stack-protector -fno-pic -Wall -Wextra -O2 -g
ASFLAGS = -m32
LDFLAGS = -m elf_i386 -T linker.ld -nostdlib -z max-page-size=0x10

OBJECTS = entry.o loader.o

all: boot.elf

boot.elf: $(OBJECTS) linker.ld
	$(LD) $(LDFLAGS) -o $@ $(OBJECTS)
	@echo "Boot stub built: boot.elf"
	@size boot.elf

entry.o: entry.S
	$(AS) $(ASFLAGS) -c -o $@ $<

loader.o: loader.c loader.h
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f *.o boot.elf

.PHONY: all clean

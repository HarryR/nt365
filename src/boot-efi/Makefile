# MicroNT UEFI loader — builds BOOTX64.EFI for x86_64 UEFI firmware
# (OVMF64). The loader itself runs in long mode; it constructs NT's
# 32-bit page tables + descriptor state in memory, then hands off
# via a 64→32 mode-drop stub in transition.S to the NT kernel at a
# KSEG0 virtual address.
#
# Pipeline:
#   .c  --(gcc -m64 -fshort-wchar ...)-->  .o
#   .o  --(ld with elf_x86_64_efi.lds)-->  loader.so  (ELF shared object)
#   .so --(objcopy PE sections)--------->  BOOTX64.EFI
#
# ESP / disk image assembly is handled by pkg/ntosbe (Lua), not here.

ARCH       := x86_64
EFI_INC    := /usr/include/efi
EFI_LIB    := /usr/lib
EFI_CRT0   := $(EFI_LIB)/crt0-efi-$(ARCH).o
EFI_LDS    := $(EFI_LIB)/elf_$(ARCH)_efi.lds

CC         := gcc
LD         := ld
OBJCOPY    := objcopy

# -mno-red-zone: UEFI has no 128-byte red zone convention; interrupts
#   can land on the stack at any point so we can't rely on SysV's red
#   zone and must disable it for freestanding + interrupt-safe code.
# -fshort-wchar: UEFI strings are UTF-16, gcc defaults to 32-bit wchar.
CFLAGS := \
    -m64 -ffreestanding -fno-stack-protector -fno-stack-check \
    -fshort-wchar -mno-red-zone \
    -Wall -Wextra -Werror \
    -I$(EFI_INC) -I$(EFI_INC)/$(ARCH) \
    -DEFI_FUNCTION_WRAPPER \
    -nostdinc \
    -isystem $(EFI_INC) -isystem $(EFI_INC)/$(ARCH) \
    -isystem $(shell $(CC) -m64 -print-file-name=include)

LDFLAGS := \
    -nostdlib -znocombreloc -T $(EFI_LDS) -shared \
    -Bsymbolic -L$(EFI_LIB) -m elf_x86_64

OBJS := main.o com1.o fs.o memmap.o mmu.o pe.o \
        arena.o uart.o hwtree.o lpb.o handoff.o fwcfg.o

all: BOOTX64.EFI

BOOTX64.EFI: loader.so
	$(OBJCOPY) \
	    -j .text -j .sdata -j .data -j .dynamic -j .dynsym -j .rel \
	    -j .rela -j .reloc \
	    --target=efi-app-$(ARCH) $< $@

loader.so: $(OBJS) $(EFI_CRT0)
	$(LD) $(LDFLAGS) $(EFI_CRT0) $(OBJS) -o $@ -lefi -lgnuefi

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.S
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) loader.so BOOTX64.EFI

.PHONY: all clean

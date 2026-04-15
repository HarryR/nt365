# MicroNT UEFI loader — builds BOOTIA32.EFI for ia32 UEFI firmware (OVMF32).
#
# Pipeline:
#   .c  --(gcc -m32 -fshort-wchar ...)-->  .o
#   .o  --(ld with elf_ia32_efi.lds)---->  loader.so  (ELF shared object)
#   .so --(objcopy PE sections)--------->  BOOTIA32.EFI
#
# Place BOOTIA32.EFI at /EFI/BOOT/BOOTIA32.EFI on the ESP.

ARCH       := ia32
EFI_INC    := /usr/include/efi
EFI_LIB    := /usr/lib32
EFI_CRT0   := $(EFI_LIB)/crt0-efi-$(ARCH).o
EFI_LDS    := $(EFI_LIB)/elf_$(ARCH)_efi.lds

CC         := gcc
LD         := ld
OBJCOPY    := objcopy

CFLAGS := \
    -m32 -ffreestanding -fno-stack-protector -fno-stack-check \
    -fshort-wchar -mno-red-zone -malign-double \
    -Wall -Wextra -Werror \
    -I$(EFI_INC) -I$(EFI_INC)/$(ARCH) \
    -DEFI_FUNCTION_WRAPPER \
    -nostdinc \
    -isystem $(EFI_INC) -isystem $(EFI_INC)/$(ARCH) \
    -isystem $(shell $(CC) -m32 -print-file-name=include)
# -malign-double: GCC's default ia32 ABI aligns UINT64 to 4, but UEFI's
# ABI aligns it to 8 — match OVMF or any struct containing EFI_LBA
# reads at shifted offsets (we hit this reading Media->LastBlock).

LDFLAGS := \
    -nostdlib -znocombreloc -T $(EFI_LDS) -shared \
    -Bsymbolic -L$(EFI_LIB) -m elf_i386

OBJS := main.o com1.o fs.o memmap.o mmu.o pe.o loaderblock.o handoff.o

# ESP image: MBR-partitioned disk with a single FAT16 partition holding
# /EFI/BOOT/BOOTIA32.EFI + a full NT System32 tree, so the single
# partition serves as both UEFI ESP and the NT boot volume. UEFI spec
# allows FAT12/16/32 equally for ESP; we use FAT16 because the kernel's
# fastfat driver is FAT12/16 only.
#
# MBR (not unpartitioned) is required so NT's atdisk sees a partition
# table at sector 0 (IoReadPartitionTable) — without one, no partition
# device objects get created and multi(0)disk(0)rdisk(0)partition(1)
# fails to resolve → STOP 0x7B. GPT support is a future step.
ESP_IMG_SIZE_MB := 64

# Partition starts at 1 MiB (sector 2048) — standard alignment; leaves
# room for MBR + historical boot code even though we don't use any.
ESP_PART_OFFSET_MB := 1
ESP_PART_SIZE_MB   := $(shell echo $$(( $(ESP_IMG_SIZE_MB) - $(ESP_PART_OFFSET_MB) )))

# Repo-relative paths to the NT build outputs that belong on the ESP.
# Each line is "<dest-on-esp>:<source-in-tree>". The order here determines
# copy order (and thus the root-dir listing order).
NT_ROOT       := ../NT
NTOS          := $(NT_ROOT)/PRIVATE/NTOS
SYSTEM_FILES := \
    System32/ntoskrnl.exe:$(NTOS)/INIT/UP/obj/i386/ntoskrnl.exe \
    System32/hal.dll:$(NTOS)/NTHALS/HAL/obj/i386/hal.dll \
    System32/ntdll.dll:$(NT_ROOT)/PUBLIC/SDK/LIB/I386/ntdll.dll \
    System32/kernel32.dll:$(NT_ROOT)/PUBLIC/SDK/LIB/I386/kernel32.dll \
    System32/smss.exe:$(NT_ROOT)/PRIVATE/SM/SERVER/obj/i386/smss.exe \
    System32/csrss.exe:$(NT_ROOT)/PRIVATE/CSR/SERVER/obj/i386/csrss.exe \
    System32/csrsrv.dll:$(NT_ROOT)/PUBLIC/SDK/LIB/I386/csrsrv.dll \
    System32/basesrv.dll:$(NT_ROOT)/PUBLIC/SDK/LIB/I386/basesrv.dll \
    System32/c_1252.nls:$(NT_ROOT)/PRIVATE/WINDOWS/WINNLS/DATA/C_1252.NLS \
    System32/c_437.nls:$(NT_ROOT)/PRIVATE/WINDOWS/WINNLS/DATA/C_437.NLS \
    System32/l_intl.nls:$(NT_ROOT)/PRIVATE/WINDOWS/WINNLS/DATA/L_INTL.NLS \
    System32/config/SYSTEM:./SYSTEM \
    System32/Drivers/atdisk.sys:$(NT_ROOT)/PUBLIC/SDK/LIB/I386/atdisk.sys \
    System32/Drivers/fastfat.sys:$(NT_ROOT)/PUBLIC/SDK/LIB/I386/fastfat.sys

# Extract just the <source> paths for prerequisites. Build the source list by
# splitting each mapping at the ':' — second half via $(word 2,$(subst :, ,X)).
SYSTEM_SRCS := $(foreach m,$(SYSTEM_FILES),$(word 2,$(subst :, ,$(m))))

all: esp.img

BOOTIA32.EFI: loader.so
	$(OBJCOPY) \
	    -j .text -j .sdata -j .data -j .dynamic -j .dynsym -j .rel \
	    -j .rela -j .reloc \
	    --target=efi-app-$(ARCH) $< $@

loader.so: $(OBJS) $(EFI_CRT0)
	$(LD) $(LDFLAGS) $(EFI_CRT0) $(OBJS) -o $@ -lefi -lgnuefi
	# Complain if there are unresolved relocs
	@if ! $(LD) $(LDFLAGS) $(EFI_CRT0) $(OBJS) -lefi -lgnuefi -o $@ 2>&1 | tee /tmp/efi_ld.log; then \
	    grep -q 'undefined' /tmp/efi_ld.log && exit 1; \
	fi

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.S
	$(CC) $(CFLAGS) -c $< -o $@

# Regenerate the SYSTEM hive from tools/mkhive.py whenever that script
# changes. The hive is tiny; rebuilding it every image run is fine.
SYSTEM: ../tools/mkhive.py
	python3 ../tools/mkhive.py $@

esp.img: BOOTIA32.EFI $(SYSTEM_SRCS)
	@# Verify every required source exists before touching the image
	@missing=""; for f in $(SYSTEM_SRCS); do \
	    [ -f "$$f" ] || missing="$$missing $$f"; \
	done; \
	if [ -n "$$missing" ]; then \
	    echo "ERROR: esp.img requires the following files (run ../build.sh):"; \
	    for f in $$missing; do echo "  - $$f"; done; \
	    exit 1; \
	fi
	dd if=/dev/zero of=$@ bs=1M count=$(ESP_IMG_SIZE_MB) status=none
	@# MBR partition table: one partition, type 0x06 (FAT16), bootable,
	@# starting at sector 2048 (1 MiB). Classic sfdisk script syntax:
	@# `start,size,type,bootable` with empty size = "to end of disk".
	@printf '2048,,6,*\n' | sfdisk --quiet --no-reread $@
	@# Format the partition via mtools's @@offset syntax. No -F flag so
	@# mformat auto-picks FAT16 for partitions under ~2 GB.
	mformat -i $@@@$(ESP_PART_OFFSET_MB)M
	mmd     -i $@@@$(ESP_PART_OFFSET_MB)M ::/EFI ::/EFI/BOOT ::/System32 ::/System32/config ::/System32/Drivers
	mcopy   -i $@@@$(ESP_PART_OFFSET_MB)M BOOTIA32.EFI ::/EFI/BOOT/BOOTIA32.EFI
	@# Copy each system file to its destination path on the image
	@for pair in $(SYSTEM_FILES); do \
	    dest=":`echo $$pair | cut -d: -f1`"; \
	    src=`echo $$pair | cut -d: -f2`; \
	    echo "  -> $$dest  ($$src)"; \
	    mcopy -i $@@@$(ESP_PART_OFFSET_MB)M "$$src" "::$${dest#:}" || exit 1; \
	done

clean:
	rm -f $(OBJS) loader.so BOOTIA32.EFI esp.img SYSTEM
	rm -f *.out *.log *.fd

# Inspect the built ESP: partition table + FAT variant + recursive listing.
IMG := esp.img@@$(ESP_PART_OFFSET_MB)M
info: esp.img
	@echo "--- partitions ---"
	@sfdisk --list esp.img
	@echo "--- minfo ---"
	@minfo -i $(IMG) | grep -Ei 'fat|cluster|sector|size|serial|name' || true
	@echo "--- mdir / ---"
	@mdir -i $(IMG) ::/
	@echo "--- mdir /EFI/BOOT ---"
	@mdir -i $(IMG) ::/EFI/BOOT
	@echo "--- mdir /System32 ---"
	@mdir -i $(IMG) ::/System32
	@echo "--- mdir /System32/config ---"
	@mdir -i $(IMG) ::/System32/config
	@echo "--- mdir /System32/Drivers ---"
	@mdir -i $(IMG) ::/System32/Drivers

.PHONY: all clean info

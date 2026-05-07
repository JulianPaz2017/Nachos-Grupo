# Use normal `make` for this Makefile.
#
# Makefile for building user programs to run on top of Nachos.

GCC_PREFIX = mipsel-linux-gnu-
LDFLAGS    = -T arrangement.ld -N -s
ASFLAGS    = -mips1
CPPFLAGS   = $(INCLUDE_DIRS)

CC  = $(GCC_PREFIX)gcc
AS  = $(GCC_PREFIX)as
LD  = $(GCC_PREFIX)ld
CPP = $(GCC_PREFIX)cpp-10

INCLUDE_DIRS = -I../userprog -I../threads
CFLAGS       = -std=c99 -G 0 -c $(INCLUDE_DIRS) -mips1 -mfp32 \
               -nostdlib -nostartfiles -nodefaultlibs -fno-pic -mno-abicalls

PROGRAMS = echo filetest halt matmult shell sort tinyshell touch cat cp rm

.PHONY: all clean

all: $(PROGRAMS)

clean:
	@echo ":: Cleaning $$(tput bold)$(notdir $(CURDIR))$$(tput sgr0)"
	@$(RM) *.o *.coff $(PROGRAMS) || true
	@$(RM) SWAP.* || true

start.o: start.s ../userprog/syscall.h
	@echo ":: Compiling $$(tput bold)$@$$(tput sgr0)"
	@$(CPP) $(CPPFLAGS) $< >strt.s
	@$(AS) $(ASFLAGS) -o $@ strt.s
	@$(RM) strt.s

lib.o: lib.c lib.h
	@echo ":: Compiling $$(tput bold)$@$$(tput sgr0)"
	@$(CC) $(CFLAGS) -c lib.c

%.o: %.c
	@echo ":: Compiling $$(tput bold)$@$$(tput sgr0)"
	@$(CC) $(CFLAGS) -c $^

$(PROGRAMS): %: %.o start.o lib.o
	@echo ":: Linking and converting $$(tput bold)$@$$(tput sgr0)"
	@$(LD) $(LDFLAGS) start.o lib.o $*.o -o $*.coff
	@../bin/coff2noff $*.coff $@

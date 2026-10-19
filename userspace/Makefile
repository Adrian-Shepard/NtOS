CC      := gcc
LD      := ld
NASM    := nasm
TAR     := tar

CFLAGS := -ffreestanding -nostdlib -fno-stack-protector -fno-pie \
          -mno-red-zone -mcmodel=large -O2 \
          -I libc/include -I libc

LDFLAGS := -nostdlib -static -T libc/user.ld

LIBC_OBJS := libc/crt0.o libc/syscall.o libc/printf.o libc/string.o libc/synth_user.o

PROGRAMS := bin/hello/hello.elf \
            bin/cat/cat.elf \
            bin/ls/ls.elf \
            bin/echo/echo.elf \
            bin/sleep/sleep.elf \
            bin/synth/synth.elf \
            bin/midi2ntsad/midi2ntsad.elf \
            bin/sh/sh.elf

STAGE := stage

all: initrd.tar

libc/crt0.o: libc/crt0.asm
	$(NASM) -f elf64 $< -o $@

libc/syscall.o: libc/syscall.asm
	$(NASM) -f elf64 $< -o $@

libc/printf.o: libc/printf.c
	$(CC) $(CFLAGS) -c $< -o $@

libc/string.o: libc/string.c
	$(CC) $(CFLAGS) -c $< -o $@

libc/synth_user.o: libc/synth_user.c
	$(CC) $(CFLAGS) -c $< -o $@

bin/hello/hello.elf: bin/hello/main.c $(LIBC_OBJS) libc/user.ld
	$(CC) $(CFLAGS) -c bin/hello/main.c -o bin/hello/main.o
	$(LD) $(LDFLAGS) $(LIBC_OBJS) bin/hello/main.o -o $@

bin/cat/cat.elf: bin/cat/main.c $(LIBC_OBJS) libc/user.ld
	$(CC) $(CFLAGS) -c bin/cat/main.c -o bin/cat/main.o
	$(LD) $(LDFLAGS) $(LIBC_OBJS) bin/cat/main.o -o $@

bin/ls/ls.elf: bin/ls/main.c $(LIBC_OBJS) libc/user.ld
	$(CC) $(CFLAGS) -c bin/ls/main.c -o bin/ls/main.o
	$(LD) $(LDFLAGS) $(LIBC_OBJS) bin/ls/main.o -o $@

bin/echo/echo.elf: bin/echo/main.c $(LIBC_OBJS) libc/user.ld
	$(CC) $(CFLAGS) -c bin/echo/main.c -o bin/echo/main.o
	$(LD) $(LDFLAGS) $(LIBC_OBJS) bin/echo/main.o -o $@

bin/sleep/sleep.elf: bin/sleep/main.c $(LIBC_OBJS) libc/user.ld
	$(CC) $(CFLAGS) -c bin/sleep/main.c -o bin/sleep/main.o
	$(LD) $(LDFLAGS) $(LIBC_OBJS) bin/sleep/main.o -o $@

bin/synth/synth.elf: bin/synth/main.c $(LIBC_OBJS) libc/user.ld
	$(CC) $(CFLAGS) -c bin/synth/main.c -o bin/synth/main.o
	$(LD) $(LDFLAGS) $(LIBC_OBJS) bin/synth/main.o -o $@

bin/midi2ntsad/midi2ntsad.elf: bin/midi2ntsad/main.c $(LIBC_OBJS) libc/user.ld
	$(CC) $(CFLAGS) -c bin/midi2ntsad/main.c -o bin/midi2ntsad/main.o
	$(LD) $(LDFLAGS) $(LIBC_OBJS) bin/midi2ntsad/main.o -o $@

bin/sh/sh.elf: bin/sh/main.c $(LIBC_OBJS) libc/user.ld
	$(CC) $(CFLAGS) -c bin/sh/main.c -o bin/sh/main.o
	$(LD) $(LDFLAGS) $(LIBC_OBJS) bin/sh/main.o -o $@

$(STAGE):
	mkdir -p $(STAGE)

initrd.tar: $(PROGRAMS) bin/midi2ntsad/test.mid | $(STAGE)
	rm -f $(STAGE)/*.elf $(STAGE)/*.mid
	cp $(PROGRAMS) $(STAGE)/
	cp bin/midi2ntsad/test.mid $(STAGE)/
	cd $(STAGE) && $(TAR) --format=ustar -cf ../initrd.tar *
	@echo "created initrd.tar with:"
	@$(TAR) -tf initrd.tar | sed 's/^/  /'

clean:
	rm -f libc/*.o bin/*/*.o $(PROGRAMS) initrd.tar
	rm -rf $(STAGE)

.PHONY: all clean
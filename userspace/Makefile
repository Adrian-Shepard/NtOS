# userspace/Makefile
CC      = gcc
LD      = ld
CFLAGS  = -ffreestanding -fno-pie -fno-stack-protector -mno-red-zone -m64 -mcmodel=large -Wall -O2 -I libc/include -I libc
LDFLAGS = -nostdlib -static -T libc/user.ld -z max-page-size=0x1000

PROGS = hello echo cat ls sleep sh synth midi2ntsad midiplay

.PHONY: all clean

all: libc.a $(PROGS) initrd.tar

libc.a: libc/crt0.o libc/printf.o libc/string.o libc/synth_user.o libc/syscall.o
	ar rcs $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.asm
	nasm -f elf64 $< -o $@

hello: bin/hello/main.o libc.a
	$(LD) $(LDFLAGS) -o bin/hello/hello.elf libc/crt0.o $< libc.a
cat: bin/cat/main.o libc.a
	$(LD) $(LDFLAGS) -o bin/cat/cat.elf libc/crt0.o $< libc.a
ls: bin/ls/main.o libc.a
	$(LD) $(LDFLAGS) -o bin/ls/ls.elf libc/crt0.o $< libc.a
echo: bin/echo/main.o libc.a
	$(LD) $(LDFLAGS) -o bin/echo/echo.elf libc/crt0.o $< libc.a
sleep: bin/sleep/main.o libc.a
	$(LD) $(LDFLAGS) -o bin/sleep/sleep.elf libc/crt0.o $< libc.a
sh: bin/sh/main.o libc.a
	$(LD) $(LDFLAGS) -o bin/sh/sh.elf libc/crt0.o $< libc.a
synth: bin/synth/main.o libc.a
	$(LD) $(LDFLAGS) -o bin/synth/synth.elf libc/crt0.o $< libc.a
midi2ntsad: bin/midi2ntsad/main.o libc.a
	$(LD) $(LDFLAGS) -o bin/midi2ntsad/midi2ntsad.elf libc/crt0.o $< libc.a
midiplay: bin/midiplay/main.o libc.a
	$(LD) $(LDFLAGS) -o bin/midiplay/midiplay.elf libc/crt0.o $< libc.a

initrd.tar: $(PROGS)
	mkdir -p stage
	cp bin/hello/hello.elf stage/
	cp bin/cat/cat.elf stage/
	cp bin/ls/ls.elf stage/
	cp bin/echo/echo.elf stage/
	cp bin/sleep/sleep.elf stage/
	cp bin/sh/sh.elf stage/
	cp bin/synth/synth.elf stage/
	cp bin/midi2ntsad/midi2ntsad.elf stage/
	cp bin/midiplay/midiplay.elf stage/
	cp bin/midi2ntsad/test.mid stage/
	cd stage && tar -cf ../initrd.tar *

clean:
	rm -f libc/*.o bin/*/*.o bin/hello/hello.elf bin/cat/cat.elf bin/ls/ls.elf bin/echo/echo.elf bin/sleep/sleep.elf bin/synth/synth.elf bin/midi2ntsad/midi2ntsad.elf bin/sh/sh.elf bin/midiplay/midiplay.elf initrd.tar
	rm -rf stage
CC      = gcc
LD      = ld
CFLAGS  = -ffreestanding -fno-pie -fno-stack-protector -mno-red-zone -m64 -mcmodel=large -MMD -MP -Wall -O2 -I libc/include -I libc
LDFLAGS = -nostdlib -static -T libc/user.ld -z max-page-size=0x1000

# Находим только папки (слэш в конце)
PROGS := $(patsubst bin/%/,%,$(wildcard bin/*/))

.PHONY: all clean

all: libc.a $(PROGS) initrd.tar

libc.a: libc/crt0.o libc/printf.o libc/string.o libc/synth_user.o libc/syscall.o
	ar rcs $@ $^

%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.asm
	@mkdir -p $(dir $@)
	nasm -f elf64 $< -o $@

$(PROGS): %: bin/%/main.o libc.a
	@mkdir -p bin/$*
	$(LD) $(LDFLAGS) -o bin/$*/$*.elf libc/crt0.o $< libc.a

initrd.tar: $(PROGS)
	mkdir -p stage
	cp bin/*/*.elf stage/
	cp bin/midi2ntsad/test.mid stage/ 2>/dev/null || true
	cd stage && tar -cf ../initrd.tar *

# Подключаем зависимости для userspace
-include $(wildcard libc/*.d)
-include $(wildcard bin/*/*.d)

clean:
	rm -f libc/*.o libc/*.d bin/*/*.o bin/*/*.d bin/*/*.elf initrd.tar
	rm -rf stage
	@echo "Cleaned userspace build."
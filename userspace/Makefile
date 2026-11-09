CC      = gcc
LD      = ld
CFLAGS  = -ffreestanding -fno-pie -fno-stack-protector -mno-red-zone -m64 -mcmodel=large -MMD -MP -Wall -O2 -I libc/include -I libc
LDFLAGS = -nostdlib -static -T libc/user.ld -z max-page-size=0x1000

# Находим все программы в apps/ и tests/
APP_PROGS := $(patsubst apps/%/,%,$(wildcard apps/*/))
TEST_PROGS := $(patsubst tests/%/,%,$(wildcard tests/*/))

ALL_PROGS := $(APP_PROGS) $(TEST_PROGS)

.PHONY: all clean

all: libc.a $(ALL_PROGS) initrd.tar

libc.a: libc/crt0.o libc/printf.o libc/string.o libc/synth_user.o libc/syscall.o
	ar rcs $@ $^

%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.asm
	@mkdir -p $(dir $@)
	nasm -f elf64 $< -o $@

# Правила для приложений
$(APP_PROGS): %: apps/%/main.o libc.a
	@mkdir -p apps/$*
	$(LD) $(LDFLAGS) -o apps/$*/$*.elf libc/crt0.o $< libc.a

# Правила для тестов
$(TEST_PROGS): %: tests/%/main.o libc.a
	@mkdir -p tests/$*
	$(LD) $(LDFLAGS) -o tests/$*/$*.elf libc/crt0.o $< libc.a

initrd.tar: $(ALL_PROGS)
	mkdir -p stage
	cp apps/*/*.elf stage/
	cp tests/*/*.elf stage/
	cp apps/midi2ntsad/test.mid stage/ 2>/dev/null || true
	cd stage && tar -cf ../initrd.tar *

-include $(wildcard apps/*/*.d)
-include $(wildcard tests/*/*.d)
-include $(wildcard libc/*.d)

clean:
	rm -f libc/*.o libc/*.d apps/*/*.o apps/*/*.d apps/*/*.elf tests/*/*.o tests/*/*.d tests/*/*.elf initrd.tar
	rm -rf stage
	@echo "Cleaned userspace build."
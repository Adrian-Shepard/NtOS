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

# Иерархия файловой системы как в Linux/Windows:
#   bin/   - исполняемые файлы (ELF)
#   etc/   - конфигурация (motd, desktop.cfg)
#   home/  - пользовательские файлы
initrd.tar: $(ALL_PROGS)
	rm -rf stage
	mkdir -p stage/bin stage/etc stage/home
	cp apps/*/*.elf stage/bin/
	cp tests/*/*.elf stage/bin/
	cp apps/midi2ntsad/test.mid stage/etc/ 2>/dev/null || true
	cp etc/motd stage/etc/motd 2>/dev/null || true
	cp etc/desktop.cfg stage/etc/desktop.cfg 2>/dev/null || true
	printf 'Welcome to the NtOS home directory.\n' > stage/home/readme.txt
	cd stage && tar -cf ../initrd.tar bin etc home

-include $(wildcard apps/*/*.d)
-include $(wildcard tests/*/*.d)
-include $(wildcard libc/*.d)

clean:
	rm -f libc/*.o libc/*.d apps/*/*.o apps/*/*.d apps/*/*.elf tests/*/*.o tests/*/*.d tests/*/*.elf initrd.tar
	rm -rf stage
	@echo "Cleaned userspace build."
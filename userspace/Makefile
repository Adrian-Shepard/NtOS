CC      = gcc
LD      = ld
CFLAGS  = -ffreestanding -fno-pie -fno-stack-protector -mno-red-zone -m64 -mcmodel=large -MMD -MP -Wall -O2 -I libc/include -I libc
LDFLAGS = -nostdlib -static -T libc/user.ld -z max-page-size=0x1000

# Doom собирается отдельно (со своим мини-libc doom_libc.c), поэтому
# исключаем его из общего списка приложений.
APP_PROGS := $(filter-out doom,$(patsubst apps/%/,%,$(wildcard apps/*/)))
TEST_PROGS := $(patsubst tests/%/,%,$(wildcard tests/*/))

ALL_PROGS := $(APP_PROGS) $(TEST_PROGS)

.PHONY: all clean

all: libc.a $(ALL_PROGS) $(DOOM_ELF) initrd.tar

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

# ======================= DOOM (userspace) =======================
DOOM_DIR  := ../doomgeneric/doomgeneric
DOOM_INC  := ../centcom/apps/doom_include
DOOM_ELF  := apps/doom/doom.elf

DOOM_SRCS := $(DOOM_DIR)/doomgeneric.c \
  $(DOOM_DIR)/am_map.c \
  $(DOOM_DIR)/d_event.c $(DOOM_DIR)/d_items.c $(DOOM_DIR)/d_iwad.c \
  $(DOOM_DIR)/d_loop.c $(DOOM_DIR)/d_main.c $(DOOM_DIR)/d_mode.c \
  $(DOOM_DIR)/d_net.c $(DOOM_DIR)/doomdef.c $(DOOM_DIR)/doomstat.c \
  $(DOOM_DIR)/dstrings.c $(DOOM_DIR)/dummy.c \
  $(DOOM_DIR)/f_finale.c $(DOOM_DIR)/f_wipe.c \
  $(DOOM_DIR)/g_game.c $(DOOM_DIR)/gusconf.c \
  $(DOOM_DIR)/hu_lib.c $(DOOM_DIR)/hu_stuff.c \
  $(DOOM_DIR)/i_cdmus.c $(DOOM_DIR)/i_joystick.c \
  $(DOOM_DIR)/i_input.c $(DOOM_DIR)/i_scale.c $(DOOM_DIR)/i_sound.c \
  $(DOOM_DIR)/i_system.c $(DOOM_DIR)/i_timer.c $(DOOM_DIR)/i_video.c \
  $(DOOM_DIR)/i_endoom.c \
  $(DOOM_DIR)/icon.c $(DOOM_DIR)/info.c \
  $(DOOM_DIR)/m_argv.c $(DOOM_DIR)/m_bbox.c $(DOOM_DIR)/m_cheat.c \
  $(DOOM_DIR)/m_config.c $(DOOM_DIR)/m_controls.c $(DOOM_DIR)/m_fixed.c \
  $(DOOM_DIR)/m_menu.c $(DOOM_DIR)/m_misc.c $(DOOM_DIR)/m_random.c \
  $(DOOM_DIR)/memio.c $(DOOM_DIR)/mus2mid.c \
  $(DOOM_DIR)/p_ceilng.c $(DOOM_DIR)/p_doors.c $(DOOM_DIR)/p_enemy.c \
  $(DOOM_DIR)/p_floor.c $(DOOM_DIR)/p_inter.c $(DOOM_DIR)/p_lights.c \
  $(DOOM_DIR)/p_map.c $(DOOM_DIR)/p_maputl.c $(DOOM_DIR)/p_mobj.c \
  $(DOOM_DIR)/p_plats.c $(DOOM_DIR)/p_pspr.c $(DOOM_DIR)/p_saveg.c \
  $(DOOM_DIR)/p_setup.c $(DOOM_DIR)/p_sight.c $(DOOM_DIR)/p_spec.c \
  $(DOOM_DIR)/p_switch.c $(DOOM_DIR)/p_telept.c $(DOOM_DIR)/p_tick.c \
  $(DOOM_DIR)/p_user.c \
  $(DOOM_DIR)/r_bsp.c $(DOOM_DIR)/r_data.c $(DOOM_DIR)/r_draw.c \
  $(DOOM_DIR)/r_main.c $(DOOM_DIR)/r_plane.c $(DOOM_DIR)/r_segs.c \
  $(DOOM_DIR)/r_sky.c $(DOOM_DIR)/r_things.c \
  $(DOOM_DIR)/s_sound.c $(DOOM_DIR)/sha1.c $(DOOM_DIR)/sounds.c \
  $(DOOM_DIR)/st_lib.c $(DOOM_DIR)/st_stuff.c $(DOOM_DIR)/statdump.c \
  $(DOOM_DIR)/tables.c \
  $(DOOM_DIR)/v_video.c \
  $(DOOM_DIR)/w_checksum.c $(DOOM_DIR)/w_file.c $(DOOM_DIR)/w_file_stdc.c \
  $(DOOM_DIR)/w_main.c $(DOOM_DIR)/w_wad.c \
  $(DOOM_DIR)/wi_stuff.c \
  $(DOOM_DIR)/z_zone.c

DOOM_OBJS := $(patsubst $(DOOM_DIR)/%.c,apps/doom/%.o,$(DOOM_SRCS))
DOOM_OBJS += apps/doom/main.o apps/doom/doom_platform.o apps/doom/doom_libc.o

# Заголовки doom_include должны идти ПЕРЕД libc/include, иначе Doom получит
# минимальные stdio.h/string.h из userspace-либки вместо полноценных.
DOOM_CFLAGS := -ffreestanding -fno-pie -fno-stack-protector -mno-red-zone -m64 \
               -mcmodel=large -MMD -MP -Wall -O2 \
               -I $(DOOM_INC) -I $(DOOM_DIR) -I libc/include -I libc

# Более специфичные правила имеют приоритет над общим %.o: %.c
apps/doom/%.o: apps/doom/%.c
	@mkdir -p apps/doom
	$(CC) $(DOOM_CFLAGS) -c $< -o $@

apps/doom/%.o: $(DOOM_DIR)/%.c
	@mkdir -p apps/doom
	$(CC) $(DOOM_CFLAGS) -c $< -o $@

$(DOOM_ELF): $(DOOM_OBJS) libc/crt0.o libc/syscall.o
	$(LD) $(LDFLAGS) -o $@ libc/crt0.o libc/syscall.o $(DOOM_OBJS)
# ================================================================

# Иерархия файловой системы (Linux/Windows-стиль):
#   bin/    исполняемые файлы (ELF)
#   etc/    конфигурация (motd, desktop.cfg)
#   home/   пользовательские файлы
#   share/  ресурсы (doom1.wad)
initrd.tar: $(ALL_PROGS) $(DOOM_ELF)
	rm -rf stage
	mkdir -p stage/bin stage/etc stage/home stage/share
	cp apps/*/*.elf stage/bin/
	cp tests/*/*.elf stage/bin/
	cp apps/midi2ntsad/test.mid stage/etc/ 2>/dev/null || true
	cp etc/motd stage/etc/motd 2>/dev/null || true
	cp etc/desktop.cfg stage/etc/desktop.cfg 2>/dev/null || true
	cp ../doom1.wad stage/share/doom1.wad 2>/dev/null || true
	printf 'Welcome to the NtOS home directory.\n' > stage/home/readme.txt
	cd stage && tar -cf ../initrd.tar bin etc home share

-include $(wildcard apps/*/*.d)
-include $(wildcard tests/*/*.d)
-include $(wildcard libc/*.d)

clean:
	rm -f libc/*.o libc/*.d apps/*/*.o apps/*/*.d apps/*/*.elf tests/*/*.o tests/*/*.d tests/*/*.elf initrd.tar
	rm -rf stage
	@echo "Cleaned userspace build."
CC      := gcc
MINGW   := x86_64-w64-mingw32-gcc
LD      := ld
NASM    := nasm
OBJCOPY := objcopy

QEMU       := /mnt/x/OSdevTools/qemu/qemu-system-x86_64.exe
OVMF_CODE  := X:/OSdevTools/qemu/share/edk2-x86_64-code.fd
OVMF_VARS  := X:/OSdevTools/qemu/share/edk2-x86_64-vars.fd
IMG_WIN    := X:/NtOS/NtOS/ntos.img
DISK_WIN   := X:/NtOS/NtOS/disk.img
SERIAL_WIN := X:/NtOS/NtOS/serial.log

BUILD := build

BOOT_CFLAGS := -ffreestanding -fno-stack-protector -fno-stack-check \
               -mno-red-zone -mno-sse -mno-sse2 -mno-mmx \
               -Wall -Wextra -O2 -I boot -I .

CC_CFLAGS := -ffreestanding -fno-builtin -fno-stack-protector -fno-stack-check \
             -fno-lto -fno-PIE -mno-red-zone -mcmodel=kernel \
             -mno-sse -mno-sse2 -mno-mmx -MMD -MP \
             -Wall -Wextra -O2 \
             -I centcom -I centcom/core -I centcom/log -I centcom/tty \
             -I centcom/gui -I centcom/apps -I centcom/irq -I centcom/mem -I centcom/time \
             -I centcom/driver -I centcom/elf -I centcom/fs \
             -I centcom/ntsad -I centcom/sound -I centcom/sched \
             -I centcom/store -I centcom/bus -I centcom/ipc -I .

CC_LDFLAGS := -nostdlib -static -T centcom/centcom.ld -z max-page-size=0x1000

all: ntos.iso

COBJS := $(BUILD)/multiboot.o \
         $(BUILD)/dualboot.o \
         $(BUILD)/boot.o \
         $(BUILD)/panic.o \
         $(BUILD)/log.o \
         $(BUILD)/serial.o \
         $(BUILD)/fb.o \
         $(BUILD)/bootscreen.o \
         $(BUILD)/gui.o \
         $(BUILD)/desktop.o \
         $(BUILD)/uwm.o \
         $(BUILD)/apps.o \
         $(BUILD)/terminal.o \
         $(BUILD)/taskman.o \
         $(BUILD)/editor.o \
         $(BUILD)/shell.o \
         $(BUILD)/gdt.o \
         $(BUILD)/idt.o \
         $(BUILD)/isr_c.o \
         $(BUILD)/isr_asm.o \
         $(BUILD)/flush.o \
         $(BUILD)/pic.o \
         $(BUILD)/pmm.o \
         $(BUILD)/vmm.o \
         $(BUILD)/heap.o \
         $(BUILD)/pit.o \
         $(BUILD)/keyboard.o \
         $(BUILD)/mouse.o \
         $(BUILD)/usermode.o \
         $(BUILD)/usermode_asm.o \
         $(BUILD)/syscall.o \
         $(BUILD)/syscall_asm.o \
         $(BUILD)/elf.o \
         $(BUILD)/tar.o \
         $(BUILD)/ntmem_fs.o \
         $(BUILD)/ntrfs.o \
         $(BUILD)/ntsad.o \
         $(BUILD)/speaker.o \
         $(BUILD)/cue.o \
         $(BUILD)/synth.o \
         $(BUILD)/dma.o \
         $(BUILD)/sb16.o \
         $(BUILD)/pci.o \
         $(BUILD)/ahci.o \
         $(BUILD)/usb.o \
         $(BUILD)/xhci.o \
         $(BUILD)/e1000.o \
         $(BUILD)/kv.o \
         $(BUILD)/sched.o \
         $(BUILD)/context.o \
         $(BUILD)/ipc.o \
         $(BUILD)/initrd.o

# ======================= DOOM =======================
DOOM_DIR := doomgeneric/doomgeneric
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

DOOM_OBJS := $(patsubst $(DOOM_DIR)/%.c,$(BUILD)/doom_%.o,$(DOOM_SRCS))
DOOM_OBJS += $(BUILD)/doom_platform.o $(BUILD)/doom_libc.o $(BUILD)/doom_wad.o

COBJS += $(DOOM_OBJS)

DOOM_CFLAGS := $(CC_CFLAGS) -msse -msse2 -mfpmath=sse

$(BUILD)/doom_%.o: $(DOOM_DIR)/%.c
	@mkdir -p $(BUILD)
	$(CC) $(DOOM_CFLAGS) -I centcom/apps/doom_include -I $(DOOM_DIR) -c $< -o $@

$(BUILD)/doom_platform.o: centcom/apps/doomgeneric_ntos.c
	@mkdir -p $(BUILD)
	$(CC) $(DOOM_CFLAGS) -I centcom/apps/doom_include -I $(DOOM_DIR) -c $< -o $@

$(BUILD)/doom_libc.o: centcom/apps/libc_shim.c
	@mkdir -p $(BUILD)
	$(CC) $(DOOM_CFLAGS) -I centcom/apps/doom_include -c $< -o $@

$(BUILD)/doom_wad.o: doom1.wad
	@mkdir -p $(BUILD)
	$(OBJCOPY) -I binary -O elf64-x86-64 -B i386:x86-64 \
	    --redefine-sym _binary_doom1_wad_start=doom1_wad_start \
	    --redefine-sym _binary_doom1_wad_end=doom1_wad_end \
	    $< $@
# ====================================================

DEPS := $(COBJS:.o=.d)
-include $(DEPS)

userspace/initrd.tar: FORCE
	$(MAKE) -C userspace

FORCE:

$(BUILD)/initrd.o: userspace/initrd.tar
	@mkdir -p $(BUILD)
	$(OBJCOPY) -I binary -O elf64-x86-64 -B i386:x86-64 \
	    --redefine-sym _binary_userspace_initrd_tar_start=initrd_start \
	    --redefine-sym _binary_userspace_initrd_tar_end=initrd_end \
	    $< $@

$(BUILD)/multiboot.o: boot/multiboot.asm
	@mkdir -p $(BUILD)
	$(NASM) -f elf64 $< -o $@

$(BUILD)/dualboot.o: boot/dualboot.c
	@mkdir -p $(BUILD)
	$(CC) $(BOOT_CFLAGS) -c $< -o $@

$(BUILD)/isr_asm.o: centcom/irq/isr.asm             ; @mkdir -p $(BUILD); $(NASM) -f elf64 $< -o $@
$(BUILD)/syscall_asm.o: centcom/core/syscall.asm    ; @mkdir -p $(BUILD); $(NASM) -f elf64 $< -o $@
$(BUILD)/usermode_asm.o: centcom/core/usermode.asm  ; @mkdir -p $(BUILD); $(NASM) -f elf64 $< -o $@
$(BUILD)/context.o: centcom/sched/context.asm       ; @mkdir -p $(BUILD); $(NASM) -f elf64 $< -o $@
$(BUILD)/flush.o: centcom/core/flush.asm            ; @mkdir -p $(BUILD); $(NASM) -f elf64 $< -o $@
$(BUILD)/isr_c.o: centcom/irq/isr.c                 ; @mkdir -p $(BUILD); $(CC) $(CC_CFLAGS) -c $< -o $@

$(BUILD)/%.o: centcom/core/%.c        ; @mkdir -p $(BUILD); $(CC) $(CC_CFLAGS) -c $< -o $@
$(BUILD)/%.o: centcom/log/%.c         ; @mkdir -p $(BUILD); $(CC) $(CC_CFLAGS) -c $< -o $@
$(BUILD)/%.o: centcom/tty/%.c         ; @mkdir -p $(BUILD); $(CC) $(CC_CFLAGS) -c $< -o $@
$(BUILD)/%.o: centcom/gui/%.c         ; @mkdir -p $(BUILD); $(CC) $(CC_CFLAGS) -c $< -o $@
$(BUILD)/%.o: centcom/apps/%.c        ; @mkdir -p $(BUILD); $(CC) $(CC_CFLAGS) -c $< -o $@
$(BUILD)/%.o: centcom/irq/%.c         ; @mkdir -p $(BUILD); $(CC) $(CC_CFLAGS) -c $< -o $@
$(BUILD)/%.o: centcom/mem/%.c         ; @mkdir -p $(BUILD); $(CC) $(CC_CFLAGS) -c $< -o $@
$(BUILD)/%.o: centcom/time/%.c        ; @mkdir -p $(BUILD); $(CC) $(CC_CFLAGS) -c $< -o $@
# Драйверы разложены по подкатегориям: input/, audio/, storage/, network/, usb/
$(BUILD)/keyboard.o: centcom/driver/input/keyboard/keyboard.c  ; @mkdir -p $(BUILD); $(CC) $(CC_CFLAGS) -c $< -o $@
$(BUILD)/mouse.o:    centcom/driver/input/mouse/mouse.c         ; @mkdir -p $(BUILD); $(CC) $(CC_CFLAGS) -c $< -o $@
$(BUILD)/sb16.o:     centcom/driver/audio/sb16/sb16.c           ; @mkdir -p $(BUILD); $(CC) $(CC_CFLAGS) -c $< -o $@
$(BUILD)/ahci.o:     centcom/driver/storage/ahci/ahci.c         ; @mkdir -p $(BUILD); $(CC) $(CC_CFLAGS) -c $< -o $@
$(BUILD)/dma.o:      centcom/driver/storage/dma/dma.c           ; @mkdir -p $(BUILD); $(CC) $(CC_CFLAGS) -c $< -o $@
$(BUILD)/e1000.o:    centcom/driver/network/e1000/e1000.c       ; @mkdir -p $(BUILD); $(CC) $(CC_CFLAGS) -c $< -o $@
$(BUILD)/usb.o:      centcom/driver/usb/controller/usb.c        ; @mkdir -p $(BUILD); $(CC) $(CC_CFLAGS) -c $< -o $@
$(BUILD)/xhci.o:     centcom/driver/usb/xhci/xhci.c             ; @mkdir -p $(BUILD); $(CC) $(CC_CFLAGS) -c $< -o $@
$(BUILD)/%.o: centcom/elf/%.c         ; @mkdir -p $(BUILD); $(CC) $(CC_CFLAGS) -c $< -o $@
$(BUILD)/%.o: centcom/fs/%.c          ; @mkdir -p $(BUILD); $(CC) $(CC_CFLAGS) -c $< -o $@
$(BUILD)/%.o: centcom/ntsad/%.c       ; @mkdir -p $(BUILD); $(CC) $(CC_CFLAGS) -c $< -o $@
$(BUILD)/%.o: centcom/sound/%.c       ; @mkdir -p $(BUILD); $(CC) $(CC_CFLAGS) -c $< -o $@
$(BUILD)/%.o: centcom/sched/%.c       ; @mkdir -p $(BUILD); $(CC) $(CC_CFLAGS) -c $< -o $@
$(BUILD)/%.o: centcom/store/%.c       ; @mkdir -p $(BUILD); $(CC) $(CC_CFLAGS) -c $< -o $@
$(BUILD)/%.o: centcom/bus/%.c         ; @mkdir -p $(BUILD); $(CC) $(CC_CFLAGS) -c $< -o $@
$(BUILD)/%.o: centcom/ipc/%.c         ; @mkdir -p $(BUILD); $(CC) $(CC_CFLAGS) -c $< -o $@

$(BUILD)/centcom.elf: $(COBJS) centcom/centcom.ld
	$(LD) $(CC_LDFLAGS) $(COBJS) -o $@

ntos.iso: $(BUILD)/centcom.elf userspace/initrd.tar boot/grub.cfg
	@mkdir -p $(BUILD)/iso/boot/grub
	cp $(BUILD)/centcom.elf $(BUILD)/iso/boot/
	cp boot/grub.cfg $(BUILD)/iso/boot/grub/
	grub-mkrescue -o ntos.iso $(BUILD)/iso

disk.img:
	@echo "Создаю пустой disk.img (16 MiB)..."
	dd if=/dev/zero of=$@ bs=1M count=16 status=none

run: ntos.iso disk.img
	@$(QEMU) -machine q35,i8042=on -m 512 -vga std \
	    -audiodev sdl,id=snd0 -machine pcspk-audiodev=snd0 -device sb16,audiodev=snd0 \
	    -netdev user,id=net0 -device e1000,netdev=net0,romfile= \
	    -drive if=pflash,format=raw,readonly=on,file=$(OVMF_CODE) \
	    -drive if=pflash,format=raw,file=$(OVMF_VARS) \
	    -cdrom ntos.iso \
	    -device ich9-ahci,id=ahci_ctrl \
	    -drive id=disk0,format=raw,file=$(DISK_WIN),if=none \
	    -device ide-hd,drive=disk0,bus=ahci_ctrl.0 \
	    -serial file:$(SERIAL_WIN) -display sdl &

run-legacy: ntos.iso disk.img
	@$(QEMU) -machine pc,i8042=on -m 512 -vga std \
	    -audiodev sdl,id=snd0 -machine pcspk-audiodev=snd0 -device sb16,audiodev=snd0 \
	    -netdev user,id=net0 -device e1000,netdev=net0,romfile= \
	    -cdrom ntos.iso -device ahci,id=ahci \
	    -drive id=disk0,format=raw,file=$(DISK_WIN),if=none \
	    -device ide-hd,drive=disk0,bus=ahci.0 \
	    -serial file:$(SERIAL_WIN) -display sdl &

log:
	@tail -f /mnt/x/NtOS/NtOS/serial.log

stop:
	@taskkill.exe /IM qemu-system-x86_64.exe /F 2>/dev/null || true

clean:
	$(MAKE) -C userspace clean
	rm -rf $(BUILD) ntos.iso disk.img serial.log

.PHONY: all run run-legacy log stop clean FORCE
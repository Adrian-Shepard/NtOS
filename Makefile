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

CC_CFLAGS := -ffreestanding -fno-stack-protector -fno-stack-check \
             -fno-lto -fno-PIE -mno-red-zone -mcmodel=kernel \
             -mno-sse -mno-sse2 -mno-mmx -MMD -MP \
             -Wall -Wextra -O2 \
             -I centcom -I centcom/core -I centcom/log -I centcom/tty \
             -I centcom/irq -I centcom/mem -I centcom/time \
             -I centcom/driver -I centcom/elf -I centcom/fs \
             -I centcom/ntsad -I centcom/sound -I centcom/sched \
             -I centcom/store -I centcom/bus -I centcom/ipc -I .

CC_LDFLAGS := -nostdlib -static -T centcom/centcom.ld -z max-page-size=0x1000

COBJS := $(BUILD)/entry.o \
         $(BUILD)/boot.o \
         $(BUILD)/panic.o \
         $(BUILD)/log.o \
         $(BUILD)/serial.o \
         $(BUILD)/fb.o \
         $(BUILD)/bootscreen.o \
         $(BUILD)/gui.o \
         $(BUILD)/shell.o \
         $(BUILD)/gdt.o \
         $(BUILD)/idt.o \
         $(BUILD)/isr_c.o \
         $(BUILD)/isr_asm.o \
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
		 $(BUILD)/e1000.o \
         $(BUILD)/kv.o \
         $(BUILD)/sched.o \
         $(BUILD)/context.o \
         $(BUILD)/ipc.o \
         $(BUILD)/initrd.o

DEPS := $(COBJS:.o=.d)
-include $(DEPS)

all: ntos.img

# ---------- userspace ----------
userspace/initrd.tar: FORCE
	$(MAKE) -C userspace

FORCE:

$(BUILD)/initrd.o: userspace/initrd.tar
	@mkdir -p $(BUILD)
	$(OBJCOPY) -I binary -O elf64-x86-64 -B i386:x86-64 \
	    --redefine-sym _binary_userspace_initrd_tar_start=initrd_start \
	    --redefine-sym _binary_userspace_initrd_tar_end=initrd_end \
	    $< $@

# ---------- boot (UEFI PE/COFF) ----------
$(BUILD)/BOOTX64.EFI: boot/bootx64.c boot/efi.h bootinfo.h
	@mkdir -p $(BUILD)
	$(MINGW) $(BOOT_CFLAGS) -c boot/bootx64.c -o $(BUILD)/bootx64.o
	$(MINGW) -nostdlib -Wl,--subsystem,10 -Wl,-e,efi_main \
	    $(BUILD)/bootx64.o -o $@

# ---------- centcom (ELF ядро) ----------
$(BUILD)/entry.o: centcom/core/entry.asm
	@mkdir -p $(BUILD)
	$(NASM) -f elf64 $< -o $@

# Явные правила для файлов, имена которых не совпадают с исходниками
$(BUILD)/isr_c.o: centcom/irq/isr.c         ; @mkdir -p $(BUILD); $(CC) $(CC_CFLAGS) -c $< -o $@
$(BUILD)/isr_asm.o: centcom/irq/isr.asm     ; @mkdir -p $(BUILD); $(NASM) -f elf64 $< -o $@
$(BUILD)/syscall_asm.o: centcom/core/syscall.asm ; @mkdir -p $(BUILD); $(NASM) -f elf64 $< -o $@
$(BUILD)/usermode_asm.o: centcom/core/usermode.asm ; @mkdir -p $(BUILD); $(NASM) -f elf64 $< -o $@

# Шаблонные правила для всех остальных .c и .asm
$(BUILD)/%.o: centcom/core/%.c        ; @mkdir -p $(BUILD); $(CC) $(CC_CFLAGS) -c $< -o $@
$(BUILD)/%.o: centcom/log/%.c         ; @mkdir -p $(BUILD); $(CC) $(CC_CFLAGS) -c $< -o $@
$(BUILD)/%.o: centcom/tty/%.c         ; @mkdir -p $(BUILD); $(CC) $(CC_CFLAGS) -c $< -o $@
$(BUILD)/%.o: centcom/irq/%.c         ; @mkdir -p $(BUILD); $(CC) $(CC_CFLAGS) -c $< -o $@
$(BUILD)/%.o: centcom/mem/%.c         ; @mkdir -p $(BUILD); $(CC) $(CC_CFLAGS) -c $< -o $@
$(BUILD)/%.o: centcom/time/%.c        ; @mkdir -p $(BUILD); $(CC) $(CC_CFLAGS) -c $< -o $@
$(BUILD)/%.o: centcom/driver/%.c      ; @mkdir -p $(BUILD); $(CC) $(CC_CFLAGS) -c $< -o $@
$(BUILD)/%.o: centcom/elf/%.c         ; @mkdir -p $(BUILD); $(CC) $(CC_CFLAGS) -c $< -o $@
$(BUILD)/%.o: centcom/fs/%.c          ; @mkdir -p $(BUILD); $(CC) $(CC_CFLAGS) -c $< -o $@
$(BUILD)/%.o: centcom/ntsad/%.c       ; @mkdir -p $(BUILD); $(CC) $(CC_CFLAGS) -c $< -o $@
$(BUILD)/%.o: centcom/sound/%.c       ; @mkdir -p $(BUILD); $(CC) $(CC_CFLAGS) -c $< -o $@
$(BUILD)/%.o: centcom/sched/%.c       ; @mkdir -p $(BUILD); $(CC) $(CC_CFLAGS) -c $< -o $@
$(BUILD)/%.o: centcom/sched/%.asm     ; @mkdir -p $(BUILD); $(NASM) -f elf64 $< -o $@
$(BUILD)/%.o: centcom/store/%.c       ; @mkdir -p $(BUILD); $(CC) $(CC_CFLAGS) -c $< -o $@
$(BUILD)/%.o: centcom/bus/%.c         ; @mkdir -p $(BUILD); $(CC) $(CC_CFLAGS) -c $< -o $@
$(BUILD)/%.o: centcom/ipc/%.c         ; @mkdir -p $(BUILD); $(CC) $(CC_CFLAGS) -c $< -o $@

$(BUILD)/centcom.elf: $(COBJS) centcom/centcom.ld
	$(LD) $(CC_LDFLAGS) $(COBJS) -o $@

$(BUILD)/centcom.bin: $(BUILD)/centcom.elf
	$(OBJCOPY) -O binary $< $@

# ---------- образ FAT32 ----------
ntos.img: $(BUILD)/BOOTX64.EFI $(BUILD)/centcom.bin startup.nsh
	dd if=/dev/zero of=$@ bs=1M count=64 status=none
	mkfs.vfat -F 32 $@ >/dev/null
	mmd -i $@ ::/EFI
	mmd -i $@ ::/EFI/BOOT
	mcopy -i $@ $(BUILD)/BOOTX64.EFI ::/EFI/BOOT/BOOTX64.EFI
	mcopy -i $@ $(BUILD)/centcom.bin   ::/centcom.bin
	mcopy -i $@ startup.nsh            ::/startup.nsh

disk.img:
	@echo "Создаю пустой disk.img (16 MiB)..."
	dd if=/dev/zero of=$@ bs=1M count=16 status=none

# ---------- запуск ----------
run: ntos.img disk.img
	@echo "Запускаю QEMU..."
	@$(QEMU) -M q35 -m 512 \
	    -audiodev sdl,id=snd0 \
	    -machine pcspk-audiodev=snd0 \
	    -device sb16,audiodev=snd0 \
	    -netdev user,id=net0 \
	    -device e1000,netdev=net0 \
	    -drive if=pflash,format=raw,readonly=on,file=$(OVMF_CODE) \
	    -drive if=pflash,format=raw,file=$(OVMF_VARS) \
	    -drive format=raw,file=$(IMG_WIN) \
	    -device ich9-ahci,id=ahci_ctrl \
	    -drive id=disk0,format=raw,file=$(DISK_WIN),if=none \
	    -device ide-hd,drive=disk0,bus=ahci_ctrl.0 \
	    -serial file:$(SERIAL_WIN) \
	    -display sdl &

log:
	@tail -f /mnt/x/NtOS/NtOS/serial.log

stop:
	@taskkill.exe /IM qemu-system-x86_64.exe /F 2>/dev/null || true

clean:
	$(MAKE) -C userspace clean
	rm -rf $(BUILD) ntos.img disk.img serial.log
	@echo "Cleaned root build."

.PHONY: all run log stop clean FORCE
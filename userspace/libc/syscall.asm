[BITS 64]
global write
global _exit
global open
global read
global close
global sys_list
global sleep
global play_pcm
global kv_write
global kv_read
global ntrfs_list
global ntrfs_read
global ntrfs_write
global exec
global fork
global sys_wait

section .text

write:
    mov rax, 1
    syscall
    ret

_exit:
    mov rax, 0
    syscall
    hlt
    jmp _exit

open:
    mov rax, 2
    syscall
    ret

read:
    mov rax, 3
    syscall
    ret

close:
    mov rax, 4
    syscall
    ret

sys_list:
    mov rax, 5
    syscall
    ret

sleep:
    mov rax, 6
    syscall
    ret

play_pcm:
    mov rax, 7
    syscall
    ret

kv_write:
    mov rax, 8
    syscall
    ret

kv_read:
    mov rax, 9
    syscall
    ret

ntrfs_list:
    mov rax, 10
    syscall
    ret

ntrfs_read:
    mov rax, 11
    syscall
    ret

ntrfs_write:
    mov rax, 12
    syscall
    ret

exec:
    mov rax, 13
    syscall
    ret

fork:
    mov rax, 14
    syscall
    ret

sys_wait:
    mov rax, 15
    syscall
    ret
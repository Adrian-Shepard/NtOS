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
global ntrfs_format
global ntrfs_mount
global exec
global fork
global sys_wait
global ipc_create
global ipc_send
global ipc_recv
global net_send
global net_recv
global gui_win_create
global gui_win_close
global gui_win_rect
global gui_win_text
global gui_win_event
global gui_win_set_title
global get_ticks

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

ipc_create:
    mov rax, 16
    syscall
    ret

ipc_send:
    mov rax, 17
    syscall
    ret

ipc_recv:
    mov rax, 18
    syscall
    ret

ntrfs_format:
    mov rax, 19
    syscall
    ret

ntrfs_mount:
    mov rax, 20
    syscall
    ret

net_send:
    mov rax, 21
    syscall
    ret

net_recv:
    mov rax, 22
    syscall
    ret

gui_win_create:
    mov rax, 23
    syscall
    ret

gui_win_close:
    mov rax, 24
    syscall
    ret

gui_win_rect:
    mov rax, 25
    syscall
    ret

gui_win_text:
    mov rax, 26
    syscall
    ret

gui_win_event:
    mov rax, 27
    syscall
    ret

gui_win_set_title:
    mov rax, 28
    syscall
    ret

get_ticks:
    mov rax, 29
    syscall
    ret
.intel_syntax noprefix
.text
.global main

main:
  push rbp
  mov rbp, rsp
.entry:
  mov rdi, 72
  call putchar
  mov rdi, 101
  call putchar
  mov rbx, rax
  mov rdi, 108
  call putchar
  mov rdx, rax
  mov rdi, 108
  call putchar
  mov r10, rax
  mov rdi, 111
  call putchar
  mov r11, rax
  mov rdi, 10
  call putchar
  mov r12, rax
  pop rbp
  ret

.data

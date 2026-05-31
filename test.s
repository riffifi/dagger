.intel_syntax noprefix
.text
.global _start

_start:
  push rbp
  mov rbp, rsp
.entry:
  mov rax, 10
  mov rdi, rax
  call out.writeln
  mov rbx, rax
  mov rdx, 20
  mov r10, rdx
  mov rdi, r10
  call out.writeln
  mov r11, rax
  # free rdx
  mov rax, 60 # exit
  xor rdi, rdi
  syscall

.data

.intel_syntax noprefix
.text
.global out.writeln

out.writeln:
  push rbp
  mov rbp, rsp
  push rbx
  sub rsp, 32
  
  # rdi has the integer
  mov rax, rdi
  mov rcx, 10
  lea rdi, [rbp - 17] # buffer for string
  mov byte ptr [rdi], 10 # newline
  mov rbx, 1 # length
  
.Lloop:
  xor rdx, rdx
  div rcx
  add dl, '0'
  dec rdi
  mov [rdi], dl
  inc rbx
  test rax, rax
  jnz .Lloop
  
  # write(1, rsi, rdx)
  mov rsi, rdi
  mov rdx, rbx
  mov rax, 1 # write
  mov rdi, 1 # stdout
  syscall
  
  add rsp, 32
  pop rbx
  pop rbp
  ret

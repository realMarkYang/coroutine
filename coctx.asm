bits 64     ;only x64
section .text

global coctx_swap
global _coctx_swap  ;macOS

; void coctx_swap(coctx_t *curr, coctx_t *pending);

coctx_swap:
_coctx_swap:
    mov [rdi + 0],  rbx
    mov [rdi + 8],  rbp
    mov [rdi + 16], r12
    mov [rdi + 24], r13
    mov [rdi + 32], r14
    mov [rdi + 40], r15

    mov [rdi + 48], rsp ;now cache top stack
    
    mov rbx, [rsi + 0]  
    mov rbp, [rsi + 8]  
    mov r12, [rsi + 16] 
    mov r13, [rsi + 24] 
    mov r14, [rsi + 32] 
    mov r15, [rsi + 40]
    mov rsp, [rsi + 48]

    mov rdi, [rsi + 56]

    ret

    section .note.GNU-stack noalloc noexec nowrite progbits
#include <ucontext.h>
#include <stdio.h>

#define STACK_SIZE 1024*4
ucontext_t ctx_main, ctx_foo, ctx_bar;


void foo()
{
    puts("this is foo");
    puts("switch to bar");
    //Switch
    swapcontext(&ctx_foo,&ctx_bar);
    puts("back to foo");
}

void bar()
{
    puts("this is bar");
    swapcontext(&ctx_bar,&ctx_foo);
    puts("back bar again");
}


int main()
{
    char stack1[STACK_SIZE];
    char stack2[STACK_SIZE];
    //coroutine 1
    getcontext(&ctx_foo);
    ctx_foo.uc_stack.ss_sp = stack1;
    ctx_foo.uc_stack.ss_size = sizeof(stack1);
    ctx_foo.uc_link = &ctx_bar; //back to main context
    makecontext(&ctx_foo,foo,0);

    //coroutine 2
    getcontext(&ctx_bar);
    ctx_bar.uc_stack.ss_sp = stack2;
    ctx_bar.uc_stack.ss_size = sizeof(stack2);
    ctx_bar.uc_link = &ctx_main; //back to main context
    makecontext(&ctx_bar,bar,0);

    puts("main func start, ready switch to ctx_foo");
    swapcontext(&ctx_main,&ctx_foo);
    puts("all func down");
}
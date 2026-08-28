#include "coroutine.h"
#include <stdio.h>
#include <assert.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define STACK_SIZE (1024*1024)
#define DEFAULT_COROUTINE 16

typedef struct 
{
    void *regs[8];
    // regs[0]: RBX,  regs[1]: RBP,  regs[2]: R12, 
    // regs[3]: R13,  regs[4]: R14,  regs[5]: R15, 
    // regs[6]: RSP,  regs[7]: RDI
} coctx_t;

//nasm function
extern void coctx_swap(coctx_t *curr, coctx_t *pending);

struct coroutine;
struct schedule
{
    char stack[STACK_SIZE];
    coctx_t main_ctx;
    int cnt_co; //current alive coroutine count
    int cap; //capacity
    int running_co; //current running coroutine id
    struct coroutine** co; //storage coroutine pointer
};

typedef struct coroutine
{
    coroutine_func func;
    void* ud;
    coctx_t ctx;
    schedule *s;
    ptrdiff_t cap;
    ptrdiff_t size;

    int status;
    char* stack;    //own stack
} coroutine;

static coroutine* _co_new(schedule *S, coroutine_func func, void *ud)
{
    coroutine *co = malloc(sizeof(*co));
    co->cap = 0;
    co->func = func;
    co->s = S;
    co->size = 0;
    co->stack = NULL;
    co->status = C_READY;
    co->ud = ud;
    return co;
}

static void _co_close(coroutine *co)
{
    free(co->stack);
    free(co);
}



schedule *coroutine_open()
{
    schedule* S = malloc(sizeof(schedule));
    S->cap = DEFAULT_COROUTINE;
    S->cnt_co = 0;
    S->running_co = -1;
    S->co = malloc(sizeof(coroutine *) * DEFAULT_COROUTINE);
    memset(S->co, 0, sizeof(coroutine *) * S->cap);
    return S;
}

void coroutine_close(schedule *S)
{
    //stop every coroutine
    for(int i=0; i< S->cap;i++)
    {
        if(S->co[i]!=NULL)
        {
            _co_close(S->co[i]);
        }

    }
    free(S->co);
    S->co = NULL;
    free(S);
}


// mainfunc 是协程真正的入口。它是通过在协程自己的栈顶伪造一个
// "返回地址" 然后 ret 过来的，栈顶再往上并没有合法的调用者。
// 所以协程函数 co->func() 正常返回后，绝不能让 mainfunc 自己
// "裸 return"（那样会去栈外读一个假的返回地址然后跳飞崩溃），
// 必须显式 coctx_swap 切回 main_ctx。
void mainfunc(schedule *S)
{
    int id = S->running_co;
    coroutine *co = S->co[id];
    co->func(S,co->ud);
    _co_close(co);
    S->co[id] = NULL;
    --S->cnt_co;
    S->running_co = -1;

    // 这个协程已经跑完销毁了，curr 参数存到哪里都无所谓，
    // 用一个局部变量接一下即可。关键是绝不能让本函数自然 return。
    coctx_t dummy_ctx;
    coctx_swap(&dummy_ctx, &S->main_ctx);
}

int coroutine_new(schedule *S, coroutine_func func, void* ud)
{
    coroutine *co = _co_new(S,func,ud);
    if(S->cnt_co >= S->cap)
    {
        int id = S->cap;
        S->co = realloc(S->co,S->cap * 2 * sizeof(coroutine *));
        memset(S->co + S->cap,0,S->cap * sizeof(coroutine *));
        S->co[S->cap] = co;
        S->cap *=2;
        ++S->cnt_co;
        return id;
    }
    else
    {
        for(int i=0;i< S->cap;i++)
        {
            int id = ((i+S->cnt_co) % S->cap);
            if(S->co[id]==NULL)
            {
                S->co[id] = co;
                ++S->cnt_co;
                return id;
            }
        }
    }
    assert(0);
    return -1;

}




// save coroutine stack data
// 改动说明：原来这个函数是在 coroutine_yield 里、coctx_swap 之前
// 被调用的，靠一个局部变量 dummy 的地址来"猜"当前栈底。
// 但 coctx_swap 内部的 call 指令还会再往栈上 push 一次返回地址，
// 这次 push 发生在 _co_save_stack 拷贝快照之后，所以快照里永远
// 缺这最后 8 字节，导致下次 resume 时 memcpy 把真正的返回地址
// 覆盖成垃圾数据，最终 coctx_swap 里的 ret 跳到随机地址卡死。
//
// 修复：不再自己猜栈底，而是等 coctx_swap 把寄存器（包括准确的
// rsp）存进 co->ctx 之后，直接用 co->ctx.regs[6] 当作栈底来保存，
// 这样 call 压的返回地址一定会被包含进快照里。
static void _co_save_stack(coroutine* co, char* rsp, char* top)
{
    assert(top - rsp >= 0);
    assert(top - rsp <= STACK_SIZE);
    ptrdiff_t stack_size = top - rsp;

    if(co->cap < stack_size)
    {
        free(co->stack);
        co->cap = stack_size;
        co->stack = malloc(stack_size);
    }
    
    co->size = stack_size;
    memcpy(co->stack, rsp, stack_size);
}


int coroutine_status(schedule *S,int id)
{
    assert(id < S->cap && id>=0);
    if(S->co[id] ==NULL)
    {
        return C_DEAD;
    }

    return S->co[id]->status;
}


int coroutine_running(schedule *S)
{
    return S->running_co;
}


void coroutine_resume(schedule* S, int id)
{
    assert(S->running_co == -1);
    assert(id >= 0 && id < S->cap);

    coroutine *co = S->co[id];
    if(co == NULL) return;

    int status = co->status;

    switch(status)
    {
        case C_READY:
            uintptr_t stack_top = (uintptr_t)(S->stack + STACK_SIZE);
            //16字节对齐    
            stack_top &= -16L;
            //压入mainfunc的栈顶指针
            stack_top -= sizeof(void *);
            *(void **)stack_top = (void *)mainfunc;
            memset(&co->ctx, 0, sizeof(co->ctx));
            co->ctx.regs[6] = (void *)stack_top;
            co->ctx.regs[7] = (void *)S;
            S->running_co = id;
            co->status = C_RUNNING;
            coctx_swap(&S->main_ctx, &co->ctx);
        
            break;

        case C_SUSPEND:
            memcpy(S->stack + STACK_SIZE - co->size, co->stack, co->size);
            S->running_co = id;
            co->status = C_RUNNING;
            coctx_swap(&S->main_ctx, &co->ctx);
            break;

        default:
            assert(0);
    }

    // coctx_swap 返回到这里，说明协程刚刚让出（yield）或者跑完退出了。
    // 如果是 yield：co 还活着，co->status 已经在 coroutine_yield 里
    //   被设成 C_SUSPEND，co->ctx.regs[6] 是它准确的 rsp，用它把
    //   共享栈上属于这个协程的那部分内容保存下来。
    // 如果是跑完退出：mainfunc 里已经把 S->co[id] 置成 NULL 并且
    //   _co_close 释放了 co，这里绝不能再碰 co，用 S->co[id]==co
    //   这个判断来区分这两种情况。
    if(S->co[id] == co && co->status == C_SUSPEND)
    {
        _co_save_stack(co, (char *)co->ctx.regs[6], S->stack + STACK_SIZE);
    }
}

void coroutine_yield(schedule *S)
{
    int id = S->running_co;
    coroutine *co = S->co[id];
    assert((char *)&co > S->stack);

    // 改动说明：不再在这里保存栈快照（时机太早，见 _co_save_stack
    // 上方的注释），只负责切走；栈的保存挪到 coroutine_resume 里
    // coctx_swap 返回之后进行。
    co->status = C_SUSPEND;
    S->running_co = -1;
    coctx_swap(&co->ctx, &S->main_ctx);
}

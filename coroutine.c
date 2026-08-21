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





void mainfunc(schedule *S)
{
    int id = S->running_co;
    coroutine *co = S->co[id];
    co->func(S,co->ud);
    _co_close(co);
    S->co[id] = NULL;
    --S->cnt_co;
    S->running_co = -1;
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




//save coroutine stack data
void _co_save_stack(coroutine* co, char* top)
{
    char dummy = 0;
    assert(top - &dummy <= STACK_SIZE);
    ptrdiff_t stack_size = top - &dummy;
    
    if(co->cap < stack_size)
    {
        free(co->stack);
        co->cap = stack_size;
        co->stack = malloc(stack_size);
    }
    
    co->size = stack_size;
    memcpy(co->stack, &dummy, stack_size);
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

}

void coroutine_yield(schedule *S)
{
    int id = S->running_co;
    coroutine *co = S->co[id];
    assert((char *)&co > S->stack);

    _co_save_stack(co,S->stack + STACK_SIZE);
    co->status = C_SUSPEND;
    S->running_co = -1;
	coctx_swap(&co->ctx, &S->main_ctx);
}

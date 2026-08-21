#include "coroutine.h"
#include <stdio.h>
#include <ucontext.h>
#include <assert.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define STACK_SIZE (1024*1024)
#define DEFAULT_COROUTINE 16


struct coroutine;
struct schedule
{
    char stack[STACK_SIZE];
    ucontext_t main_ctx;
    int cnt_co; //current alive coroutine count
    int cap; //capacity
    int running_co; //current running coroutine id
    struct coroutine** co; //storage coroutine pointer
};

typedef struct coroutine
{
    coroutine_func func;
    void* ud;
    ucontext_t ctx;
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
    S->co = malloc(sizeof(schedule *)*DEFAULT_COROUTINE);
    memset(S->co,0,S->cap);
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





void mainfunc(uint32_t lowprt, uint32_t hprt)
{
    uintptr_t ptr = (uintptr_t)hprt << 32 | (uintptr_t)lowprt;
    schedule *S = (schedule *)ptr;

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
    memcpy(co->stack,&dummy,stack_size);   
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
    assert(id >= 0 && id <= S->cap);

    coroutine *co = S->co[id];
    if(co == NULL) return;

    int status = co->status;

    switch(status)
    {
        case C_READY:
            getcontext(&co->ctx);
            co->ctx.uc_stack.ss_sp = S->stack;
            co->ctx.uc_stack.ss_size = STACK_SIZE;
            co->ctx.uc_link = &S->main_ctx;
            S->running_co = id;
            co->status = C_RUNNING;
            uintptr_t ptr = (uintptr_t)S;
            makecontext(&co->ctx, (void (*)(void))mainfunc, 2, (uint32_t)ptr, (uint32_t)(ptr >> 32));
            swapcontext(&S->main_ctx, &co->ctx);
            break;
        
        case C_SUSPEND:
            memcpy(S->stack + STACK_SIZE - co->size, co->stack, co->size);
            S->running_co = id;
            co->status = C_RUNNING;
            swapcontext(&S->main_ctx, &co->ctx);
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
	swapcontext(&co->ctx , &S->main_ctx);
}

#ifndef COROUTINE_H
#define COROUTINE_H

//coroutine state
#define C_DEAD       0
#define C_READY      1
#define C_RUNNING    2
#define C_SUSPEND    3

typedef struct schedule schedule;

typedef void (*coroutine_func)(schedule *, void* ud);

schedule * coroutine_open();
void coroutine_close(schedule *);

int coroutine_new(schedule *, coroutine_func, void* ud);
void coroutine_resume(schedule*, int id);
int coroutine_status(schedule *,int id);
int coroutine_running(schedule *);
void coroutine_yield(schedule *);

#endif
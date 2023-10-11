#include "uthread.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <ucontext.h>


static struct uthread *current_thread = NULL;
static struct uthread *main_thread = NULL;

/// @brief 切换上下文
/// @param from 当前上下文
/// @param to 要切换到的上下文
extern void thread_switch(struct context *from, struct context *to);

void print_rsp(void *rsp) {
    printf("Switching to thread with rsp value: %p\n", rsp);
}


/// @brief 线程的入口函数
/// @param tcb 线程的控制块
/// @param thread_func 线程的执行函数
/// @param arg 线程的参数
void _uthread_entry(struct uthread *tcb, void (*thread_func)(void *),
                    void *arg);

/// @brief 清空上下文结构体
/// @param context 上下文结构体指针
static inline void make_dummy_context(struct context *context) {
  memset((struct context *)context, 0, sizeof(struct context));
}



struct uthread *uthread_create(void (*func)(void *), void *arg, const char* thread_name) {
    struct uthread *uthread = NULL;
    int ret;

    // Allocate memory for the uthread structure with 16-byte alignment
    ret = posix_memalign((void **)&uthread, 16, sizeof(struct uthread));
    if (0 != ret) {
        printf("error");
        exit(-1);
    }

    // Allocate memory for the thread's stack
    uthread->stack = malloc(STACK_SIZE);
    if(!uthread->stack) {
        printf("Failed to allocate stack for thread\n");
        free(uthread);
        return NULL;
    }

    // Initialize the context for the thread
    getcontext(&uthread->context);
    uthread->context.uc_stack.ss_sp = uthread->stack;
    uthread->context.uc_stack.ss_size = STACK_SIZE;
    uthread->context.uc_link = &main_thread->context;  // Return to main thread when done
    // makecontext(&uthread->context, (void (*)()) func, 1, arg);
    makecontext(&uthread->context, (void (*)()) _uthread_entry, 3, uthread, func, arg);


    uthread->state = THREAD_INIT;
    uthread->name = thread_name;

    printf("Thread %s created successfully.\n", thread_name);

    for (int i = 0; i < MAX_THREADS; i++) {
        if (!threads[i]) {
            threads[i] = uthread;
            printf("Thread %s created and added to slot %d\n", uthread->name, i);  // Debugging line
            break;
        }
    }

    return uthread;
}



/*
/// @brief 主线程陷入调度器，阻塞
void schedule() {
    printf("Scheduling...\n");

    struct uthread *next_thread = NULL;
    int i;

    // If the current_thread is NULL, it means we're in the main thread.
    if (!current_thread) {
        current_thread = main_thread;
    }

    // Find the next ready thread.
    for (i = 1; i < MAX_THREADS; i++) {
        printf("Checking thread in slot %d with state %d\n", i, threads[i]->state);

        if (threads[i] && (threads[i]->state == THREAD_INIT || threads[i]->state == THREAD_SUSPENDED)) {
            next_thread = threads[i];
            printf("Found next thread: %s\n", next_thread->name);
            // current_thread_index = i;
            break;
        }
    }
    printf("Selected thread %s to run next.\n", next_thread->name);


    // If no ready thread is found, return.
    if (!next_thread) {
        return;
    }
    
    // printf("current_thread->state %d\n", current_thread->state);
    // printf("next_thread->state %d\n", next_thread->state);
    // Update the states.
    current_thread->state = THREAD_SUSPENDED;
    next_thread->state = THREAD_RUNNING;
    // printf("current_thread->state %d\n", current_thread->state);
    // printf("next_thread->state %d\n", next_thread->state);
    
    printf("Switching from thread at address: %p to thread at address: %p\n", current_thread->context, next_thread->context);
    //printf("current_thread Thread %s rip value: %llx, rdi value: %llx, rsi value: %llx, rdx value: %llx\n", current_thread->name, current_thread->context.rip, current_thread->context.rdi, current_thread->context.rsi, current_thread->context.rdx);
    //printf("next_thread Thread %s rip value: %llx, rdi value: %llx, rsi value: %llx, rdx value: %llx\n", next_thread->name, next_thread->context.rip, next_thread->context.rdi, next_thread->context.rsi, next_thread->context.rdx);


    // Perform the context switch.
    printf("Switching to thread %s.\n", next_thread->name);
    // thread_switch(&current_thread->context, &next_thread->context);
    swapcontext(&current_thread->context, &next_thread->context);

    printf("Thread %s resumed with state %d.\n", current_thread->name, current_thread->state);


    // Check if the current thread has stopped and clean up if necessary
    if (current_thread->state == THREAD_STOP) {
        free(current_thread);
        threads[current_thread_index] = NULL;
    }
    // Update the current thread pointer.
    current_thread = next_thread;
}
/// @brief 线程主动让出
long long uthread_yield() {
    printf("Thread %s yielding...\n", current_thread->name);

    // Find the currently running thread
    struct uthread *prev_thread = current_thread;

    // Save the current thread's context
    if (getcontext(&prev_thread->context) == -1) {
        perror("getcontext");
        exit(EXIT_FAILURE);
    }

    // If we're returning from swapcontext (not the direct call to getcontext), skip the rest
    if (prev_thread->state != THREAD_RUNNING) {
        printf("returning from swapcontext"\n);
        return 0;
    }

    prev_thread->state = THREAD_SUSPENDED;

    // Call the scheduler to pick the next thread to run
    schedule();

    // Print debugging information
    printf("Switching from thread %s at address: %p to thread %s at address: %p\n",
           prev_thread->name, &prev_thread->context,
           current_thread->name, &current_thread->context);

    // Switch to the next thread's context
    if (swapcontext(&prev_thread->context, &current_thread->context) == -1) {
        perror("swapcontext");
        exit(EXIT_FAILURE);
    }

    // Check if the previous thread has been set to THREAD_STOP and clean up if necessary
    if (prev_thread->state == THREAD_STOP) {
        free(prev_thread->stack);
        free(prev_thread);
        threads[current_thread_index] = NULL;
    }

    return 0;
}
/// @brief 线程主动让出
long long uthread_yield() {
    printf("Thread %s yielding...\n", current_thread->name);

    // Save the current thread's context
    struct uthread *prev_thread = current_thread;

    prev_thread->state = THREAD_SUSPENDED;
    // Set the current thread's state to THREAD_SUSPENDED
    current_thread->state = THREAD_SUSPENDED;
    printf("Set state of thread %s to THREAD_SUSPENDED.\n", current_thread->name);


    // Call the scheduler to pick the next thread to run
    schedule();

    // If we're returning from swapcontext (not the direct call to getcontext), skip the rest
    if (prev_thread->returned_from_swap) {
        prev_thread->returned_from_swap = 0;  // Reset the flag
        printf("returning from swapcontext\n");
        return 0;
    }

    // Set the flag to indicate we're about to call swapcontext
    current_thread->returned_from_swap = 1;

    // Switch to the next thread's context
    if (swapcontext(&prev_thread->context, &current_thread->context) == -1) {
        perror("swapcontext");
        exit(EXIT_FAILURE);
    }

    // After returning from swapcontext, set the state of the current thread to THREAD_RUNNING
    current_thread->state = THREAD_RUNNING;

    return 0;
}
*/



long long uthread_yield() {
    printf("Thread %s yielding...\n", current_thread->name);

    printf("Before yield, state of thread %s: %d\n", current_thread->name, current_thread->state);

    // Set the current thread's state to THREAD_SUSPENDED
    current_thread->state = THREAD_SUSPENDED;
    printf("Set state of thread %s to THREAD_SUSPENDED.\n", current_thread->name);
    
    // Call the scheduler to pick the next thread to run
    schedule();
    
    // After returning from swapcontext, set the state of the current thread to THREAD_RUNNING
    current_thread->state = THREAD_RUNNING;

    return 0;
}



void schedule() {
    printf("Scheduling...\n");

    printf("Before scheduling, state of current thread %s: %d\n", current_thread->name, current_thread->state);

    struct uthread *next_thread = NULL;

    // Find the next ready thread.
    for (int i = 1; i < MAX_THREADS; i++) {
        if (threads[i]) {
            printf("Checking thread in slot %d with state %d\n", i, threads[i]->state);
            if (threads[i] && (threads[i]->state == THREAD_INIT || threads[i]->state == THREAD_SUSPENDED)) {
                next_thread = threads[i];
                printf("Found next thread: %s\n", next_thread->name);
                // current_thread_index = i;
                break;
            }
        }
    }

    // If no ready thread is found, return.
    if (!next_thread) {
        return;
    }

    printf("Selected thread %s to run next.\n", next_thread->name);

    // Debug statement before updating states
    printf("Before state update, state of current thread %s: %d, next thread %s: %d\n",
           current_thread->name, current_thread->state,
           next_thread->name, next_thread->state);

    // current_thread->state = THREAD_SUSPENDED;

    // Update the states.
    // current_thread = next_thread;
    next_thread->state = THREAD_RUNNING;
    //current_thread = next_thread;

    printf("Before swapcontext in schedule, state of current thread %s: %d, next thread %s: %d\n", 
           current_thread->name, current_thread->state, next_thread->name, next_thread->state);

    // Perform the context switch.
    printf("Switching from thread : %s to thread : %s\n", current_thread->name, next_thread->name);
    // printf("Switching to thread %s.\n", next_thread->name);
    swapcontext(&current_thread->context, &next_thread->context);

    printf("After swapcontext in schedule, state of current thread %s: %d\n", current_thread->name, current_thread->state);

    // Update the current thread pointer.
    current_thread = next_thread;
}





/// @brief 恢复线程
void uthread_resume(struct uthread *tcb) {
    // Check if the thread is in the SUSPENDED state
    if (tcb->state == THREAD_SUSPENDED) {
        // Directly switch to the thread's saved context
        if (setcontext(&tcb->context) == -1) {
            perror("setcontext");
            exit(EXIT_FAILURE);
        }
    }
}

/// @brief 销毁线程的结构体
void thread_destroy(struct uthread *tcb) {
    // Check if the thread is in the STOP state
    if (tcb->state != THREAD_STOP) {
        // Optionally, log an error or handle this case differently
        return;
    }
    // Free the memory allocated for the thread's stack
    free(tcb->stack);
    // Free the uthread structure
    free(tcb);
}



/// @brief 线程开始执行的时候，首先跳转到函数_uthread_entry,然后才进入对应的函数
void _uthread_entry(struct uthread *tcb, void (*thread_func)(void *), void *arg) {
    printf("Entered _uthread_entry for thread %s with address: %p\n", tcb->name, tcb);

    
    if (!tcb) {
        printf("Error: tcb is NULL in _uthread_entry.\n");
        return;
    }

    printf("Entering thread %s.\n", tcb->name);

    // Call the actual thread function
    thread_func(arg);

    // Once the thread function returns, mark the thread as stopped
    //tcb->state = THREAD_STOP;

    // Yield to the scheduler to pick another thread to run
    // uthread_yield();
}





/// @brief 初始化线程
void init_uthreads() {
    // Initialize global variables or data structures
    current_thread_index = 0;  // Main thread is the first thread

    // Initialize all thread slots to NULL or a default state
    for (int i = 0; i < MAX_THREADS; i++) {
        threads[i] = NULL;
    }

    // Allocate memory for the main thread's TCB
    main_thread = (struct uthread *)malloc(sizeof(struct uthread));
    if (!main_thread) {
        printf("Failed to allocate memory for main thread\n");
        exit(-1);
    }

    // Initialize the main thread's TCB
    memset(main_thread, 0, sizeof(struct uthread));
    main_thread->state = THREAD_RUNNING;
    main_thread->name = "main";

    // Allocate memory for the main thread's stack (optional)
    main_thread->stack = malloc(STACK_SIZE);
    if(!main_thread->stack) {
        printf("Failed to allocate stack for main thread\n");
        free(main_thread);
        exit(-1);
    }

    // Initialize the main thread's context
    getcontext(&main_thread->context);
    main_thread->context.uc_stack.ss_sp = main_thread->stack;
    main_thread->context.uc_stack.ss_size = STACK_SIZE;

    // Add the main thread to the threads list and set it as the current thread
    threads[0] = main_thread;
    current_thread = main_thread;
    
    printf("Main thread initialized at address: %p\n", main_thread);
    printf("Main thread name: %s, state: %d\n", main_thread->name, main_thread->state);

    printf("Thread system initialized.\n");
}

#include "threading.h"
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

// Optional: use these functions to add debug or error prints to your application
#define DEBUG_LOG(msg,...)
//#define DEBUG_LOG(msg,...) printf("threading: " msg "\n" , ##__VA_ARGS__)
#define ERROR_LOG(msg,...) printf("threading ERROR: " msg "\n" , ##__VA_ARGS__)

void* threadfunc(void* thread_param)
{

    // TODO: wait, obtain mutex, wait, release mutex as described by thread_data structure
    // hint: use a cast like the one below to obtain thread arguments from your parameter
    //struct thread_data* thread_func_args = (struct thread_data *) thread_param;
    struct thread_data* thread_func_args = (struct thread_data *) thread_param;

    // Wait before obtaining the mutex
    sleep(thread_func_args->wait_time_before);

    // Obtain the mutex
    if (pthread_mutex_lock(thread_func_args->mutex) != 0) {
        perror("pthread_mutex_lock");
        thread_func_args->thread_complete_success = false;
        return thread_param;
    }

    // Wait after obtaining the mutex
    sleep(thread_func_args->wait_time_after);

    // Release the mutex
    if (pthread_mutex_unlock(thread_func_args->mutex) != 0) {
        perror("pthread_mutex_unlock");
        thread_func_args->thread_complete_success = false;
        return thread_param;
    }

    // Indicate success
    thread_func_args->thread_complete_success = true;
    return thread_param;
}


bool start_thread_obtaining_mutex(pthread_t *thread, pthread_mutex_t *mutex,int wait_to_obtain_ms, int wait_to_release_ms)
{
    /**
     * TODO: allocate memory for thread_data, setup mutex and wait arguments, pass thread_data to created thread
     * using threadfunc() as entry point.
     *
     * return true if successful.
     *
     * See implementation details in threading.h file comment block
     */
    // Allocate memory for thread_data
    struct thread_data *thread_args = (struct thread_data *)malloc(sizeof(struct thread_data));
    if (thread_args == NULL) {
        perror("malloc");
        return false;
    }

    // Setup mutex and wait arguments
    thread_args->mutex = mutex;
    thread_args->wait_time_before = wait_to_obtain_ms / 1000; // Convert milliseconds to seconds
    thread_args->wait_time_after = wait_to_release_ms / 1000; // Convert milliseconds to seconds
    thread_args->thread_complete_success = false;

    // Create the thread
    if (pthread_create(thread, NULL, threadfunc, thread_args) != 0) {
        perror("pthread_create");
        free(thread_args);
        return false;
    }

    return true;
}


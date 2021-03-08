#define _XOPEN_SOURCE
#define _XOPEN_SOURCE_EXTENDED


#include <assert.h>
#include <curses.h>
#include <ucontext.h>
#include <string.h>
#include <time.h>
#include <stdio.h>

#include "scheduler.h"
#include "util.h"

// This is an upper limit on the number of tasks we can create.
#define MAX_TASKS 12

// This is the size of each task's stack memory
#define STACK_SIZE 65536
void invokeScheduler();

// This struct will hold the all the necessary information for each task
typedef struct task_info {
  // This field stores all the state required to switch back to this task
  ucontext_t context;

  // This field stores another context. This one is only used when the task
  // is exiting.
  ucontext_t exit_context;
  //string representation of state
  char* state;
  //1 if the task is blocked, 0 otherwise
  int blocked;
  //-1 if no tasks are blocked by it, or the number of the task
  int blockedtask;
  //contains the time to wake task
  long unsigned time2wake;
  //1 if task is blocked on a timer, 0 otherwise
  int timer_blocked;
  //set if task is blocked on user input
  int userblocked;

  int userinput;

  // TODO: Add fields here so you can:
  //   a. Keep track of this task's state.
  //   b. If the task is sleeping, when should it wake up?
  //   c. If the task is waiting for another task, which task is it waiting for?
  //   d. Was the task blocked waiting for user input? Once you successfully
  //      read input, you will need to save it here so it can be returned.
} task_info_t;

int current_task = 0; //< The handle of the currently-executing task
int num_tasks = 1;    //< The number of tasks created so far
task_info_t tasks[MAX_TASKS]; //< Information for every task
int flag = 0;
int mainblocked = 0;//set if main thread is blocked
int inputflag = 0;



void scheduler_init() {
  // TODO: Initialize the state of the scheduler
  tasks[0].state = "Running";
  tasks[0].blocked = 0;
  tasks[0].blockedtask = -1;
  tasks[0].timer_blocked = 0;
  tasks[0].userblocked = 0;
  tasks[0].userinput = ERR;

  // Allocate a stack for the new task and add it to the context
  tasks[0].context.uc_stack.ss_sp = malloc(STACK_SIZE);
  tasks[0].context.uc_stack.ss_size = STACK_SIZE;

}


void task_exit() {
  // TODO: Handle the end of a task's execution here
  task_info_t curr = tasks[current_task];

  tasks[current_task].state = "Done";
  tasks[current_task].blocked = 1;
  //check to see if exiting task has blocked another task
  if(curr.blockedtask > -1){
    if(curr.blockedtask == 0){
      mainblocked = 0;
    }
    tasks[curr.blockedtask].blocked = 0;
    //printf("task %d unblocking task %d\n", current_task, curr.blockedtask);
    tasks[curr.blockedtask].state = "Waiting";
    tasks[curr.blockedtask].blockedtask = -1;
  }

  //free(tasks[current_task].exit_context.uc_stack.ss_sp);
  //free(tasks[current_task].context.uc_stack.ss_sp);
  invokeScheduler();
}

void task_create(task_t* handle, task_fn_t fn) {
  // Claim an index for the new task
  int index = num_tasks;
  num_tasks++;

  tasks[index].state = "Waiting";
  tasks[index].blocked = 0;
  tasks[index].blockedtask = -1;
  tasks[index].timer_blocked = 0;
  tasks[index].userblocked = 0;
  tasks[index].userinput = ERR;

  // Set the task handle to this index, since task_t is just an int
  *handle = index;
  // We're going to make two contexts: one to run the task, and one that runs at the end of the task so we can clean up. Start with the second

  // First, duplicate the current context as a starting point
  getcontext(&tasks[index].exit_context);

  // Set up a stack for the exit context
  tasks[index].exit_context.uc_stack.ss_sp = malloc(STACK_SIZE);
  tasks[index].exit_context.uc_stack.ss_size = STACK_SIZE;

  // Set up a context to run when the task function returns. This should call task_exit.
  makecontext(&tasks[index].exit_context, task_exit, 0);

  // Now we start with the task's actual running context
  getcontext(&tasks[index].context);

  // Allocate a stack for the new task and add it to the context
  tasks[index].context.uc_stack.ss_sp = malloc(STACK_SIZE);
  tasks[index].context.uc_stack.ss_size = STACK_SIZE;

  // Now set the uc_link field, which sets things up so our task will go to the exit context when the task function finishes
  tasks[index].context.uc_link = &tasks[index].exit_context;

  // And finally, set up the context to execute the task function
  makecontext(&tasks[index].context, fn, 0);
}


void task_wait(task_t handle) {
  // TODO: Block this task until the specified task has exited.
  //make our current task wait for this task
  //current task is not done excecuting so we put it to sleep
  if(current_task == 0){
    mainblocked = 1;
    tasks[0].blocked = 1;
  }
  if(strcmp(tasks[handle].state, "Done") != 0){
    tasks[current_task].state = "Sleeping";
    //block current task
    tasks[current_task].blocked = 1;
    //when handle exits, we will unblock current task
    tasks[handle].blockedtask = current_task;
    invokeScheduler();
  }
}

void task_sleep(size_t ms){
  // TODO: Block this task until the requested time has elapsed.
 // Hint: Record the time the task should wake up instead of the time left for it to sleep. The bookkeeping is easier this way.
 long unsigned starttime;
 if(ms > 0){
   if(current_task == 0){
     mainblocked = 1;
     tasks[0].blocked = 1;
   }
   tasks[current_task].blocked = 1;
   starttime = time_ms();
   tasks[current_task].time2wake = starttime + ms;
   tasks[current_task].timer_blocked = 1;
   tasks[current_task].state = "Sleeping";

   //schedule next task
   invokeScheduler();
 }

}

int task_readchar(){

  tasks[current_task].userblocked = 1;
  tasks[current_task].blocked = 1;
  invokeScheduler();
  return tasks[current_task].userinput;
}



//helper method to schedule next task.
void invokeScheduler(){
  int starting_index = current_task;
  int i = 1;
  int foundtask = 0;
  int c;
  //loops through tasks to find one to run or until timer or user input is received
  while(foundtask==0){
    current_task = (i + starting_index) % num_tasks;

    //if task is blocked on a timer and it's time to wake it up
    if( (tasks[current_task].timer_blocked ==1) && (time_ms() >= tasks[current_task].time2wake)){
      tasks[current_task].timer_blocked = 0;
      tasks[current_task].blocked = 0;
      foundtask = 1;
    }
    //if task is blocked on user input and there is input available
    else if( tasks[current_task].userblocked == 1 && (c = getch()) != -1){
      //printf("user input available : %c\n",c);
      tasks[current_task].userinput = c;
      tasks[current_task].userblocked = 0;
      tasks[current_task].blocked = 0;
      inputflag = 1;
    }
    //if the main task is not blocked
    if(current_task == 0 && mainblocked == 0 && tasks[0].blocked ==0){
      foundtask = 1;
    }
    //any other task is not blocked
    else if( current_task != 0 && tasks[current_task].blocked ==0 ){
      foundtask = 1;
    }
    //we've gone through all the tasks. sleep for a bit
    if(i == num_tasks+1){
      i = 1;
    }
    else{
      i++;
    }
  }

  if(flag == 0){
    flag = 1;
    swapcontext(&tasks[0].context, &tasks[current_task].context);
  }
  if(flag == 1 && current_task == 0){
    swapcontext(&tasks[starting_index].context, &tasks[0].context);
  }
  else{
    swapcontext(&tasks[starting_index].context, &tasks[current_task].context);
  }
}

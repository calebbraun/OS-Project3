/*
 * student.c
 * This file contains the CPU scheduler for the simulation.
 * original base code from http://www.cc.gatech.edu/~rama/CS2200
 * Last modified 5/11/2016 by Sherri Goings
 */

#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "os-sim.h"
#include "student.h"

// Local helper functions
static void addReadyProcess(pcb_t* proc);
static pcb_t* getReadyProcess(void);
static void schedule(unsigned int cpu_id);
static void printReadyQueue();

/*
 * enum is useful C language construct to associate desriptive words with
 * integer values
 * in this case the variable "alg" is created to be of the given enum type,
 * which allows
 * statements like "if alg == FIFO { ...}", which is much better than "if alg ==
 * 1" where
 * you have to remember what algorithm is meant by "1"...
 * just including it here to introduce you to the idea if you haven't seen it
 * before!
 */
typedef enum {
  FIFO = 0,
  RoundRobin,
  StaticPriority
} scheduler_alg;

scheduler_alg alg;

// declare other global vars
int time_slice = -1;
int cpu_count;

/*
 * main() parses command line arguments, initializes globals, and starts
 * simulation
 */
int main(int argc, char* argv[]) {
  /* Parse command line args - must include num_cpus as first, rest optional
   * Default is to simulate using just FIFO on given num cpus, if 2nd arg given:
   * if -r, use round robin to schedule (must be 3rd arg of time_slice)
   * if -p, use static priority to schedule
   */
  if (argc == 2) {
    alg = FIFO;
    printf("running with basic FIFO\n");
  } else if (argc > 2 && strcmp(argv[2], "-r") == 0 && argc > 3) {
    alg = RoundRobin;
    time_slice = atoi(argv[3]);
    printf("running with round robin, time slice = %d\n", time_slice);
  } else if (argc > 2 && strcmp(argv[2], "-p") == 0) {
    alg = StaticPriority;
    printf("running with static priority\n");
  } else {
    fprintf(stderr,
            "Usage: ./os-sim <# CPUs> [ -r <time slice> | -p ]\n"
            "    Default : FIFO Scheduler\n"
            "         -r : Round-Robin Scheduler (must also give time slice)\n"
            "         -p : Static Priority Scheduler\n\n");
    return -1;
  }
  fflush(stdout);

  /* atoi converts string to integer */
  cpu_count = atoi(argv[1]);

  /* Allocate the current[] array and its mutex */
  current = malloc(sizeof(pcb_t*) * cpu_count);
  int i;
  for (i = 0; i < cpu_count; i++) {
    current[i] = NULL;
  }
  assert(current != NULL);
  pthread_mutex_init(&current_mutex, NULL);

  /* Initialize other necessary synch constructs */
  pthread_mutex_init(&ready_mutex, NULL);
  pthread_cond_init(&ready_empty, NULL);

  /* Start the simulator in the library */
  printf("starting simulator\n");
  fflush(stdout);
  start_simulator(cpu_count);

  return 0;
}

/*
 * idle() is called by the simulator when the idle process is scheduled.
 * It blocks until a process is added to the ready queue, and then calls
 * schedule() to select the next process to run on the CPU.
 *
 * THIS FUNCTION IS ALREADY COMPLETED - DO NOT MODIFY
 */
extern void idle(unsigned int cpu_id) {
  printf("idle(%d)\n", cpu_id);
  fflush(stdout);
  pthread_mutex_lock(&ready_mutex);
  while (head == NULL) {
    pthread_cond_wait(&ready_empty, &ready_mutex);
  }
  pthread_mutex_unlock(&ready_mutex);
  schedule(cpu_id);
}

/*
 * schedule() is your CPU scheduler. It currently implements basic FIFO
 * scheduling -
 * 1. calls getReadyProcess to select and remove a runnable process from your
 * ready queue
 * 2. updates the current array to show this process (or NULL if there was none)
 * as running on the given cpu
 * 3. sets this process state to running (unless its the NULL process)
 * 4. calls context_switch to actually start the chosen process on the given cpu
 *    - note if proc==NULL the idle process will be run
 *    - note the final arg of -1 means there is no clock interrupt
 *	context_switch() is prototyped in os-sim.h. Look there for more
 * information.
 * a basic getReadyProcess() is implemented below, look at the comments for
 * info.
 *
 * TO-DO: handle scheduling with a time-slice when necessary
 *
 * THIS FUNCTION IS PARTIALLY COMPLETED - REQUIRES MODIFICATION
 */
static void schedule(unsigned int cpu_id) {
  pcb_t* proc = getReadyProcess();

  pthread_mutex_lock(&current_mutex);
  current[cpu_id] = proc;
  pthread_mutex_unlock(&current_mutex);

  if (proc != NULL) {
    proc->state = PROCESS_RUNNING;
  }

  if (alg == FIFO) {
    context_switch(cpu_id, proc, -1);
  } else if (alg == RoundRobin) {
    // select a process from the ready queue
    // call context_switch with appropriate time slice
    context_switch(cpu_id, proc, time_slice);
    // if no ready processes, idle
  } else if (alg == StaticPriority) {
    if (proc != NULL) {
      printf("Running %s on CPU %d\n", proc->name, cpu_id);
      fflush(stdout);
    }
    context_switch(cpu_id, proc, -1);
  }
}

/*
 * preempt() is called when a process is preempted due to its timeslice
 * expiring.
 *
 * This function should place the currently running process back in the
 * ready queue, then call schedule() to select a new runnable process.
 *
 * THIS FUNCTION MUST BE IMPLEMENTED FOR ROUND ROBIN OR PRIORITY SCHEDULING
 */
extern void preempt(unsigned int cpu_id) {
  printf("preempt()\n");
  fflush(stdout);
  printReadyQueue();

  pthread_mutex_lock(&current_mutex);
  pcb_t* proc = current[cpu_id];
  pthread_mutex_unlock(&current_mutex);

  addReadyProcess(proc);

  printReadyQueue();

  schedule(cpu_id);
}

/*
 * yield() is called by the simulator when a process performs an I/O request
 * note this is different than the concept of yield in user-level threads!
 * In this context, yield sets the state of the process to waiting (on I/O),
 * then calls schedule() to select a new process to run on this CPU.
 * args: int - id of CPU process wishing to yield is currently running on.
 *
 * THIS FUNCTION IS ALREADY COMPLETED - DO NOT MODIFY
 */
extern void yield(unsigned int cpu_id) {
  // use lock to ensure thread-safe access to current process
  printf("yield(%d)\n", cpu_id);
  fflush(stdout);
  pthread_mutex_lock(&current_mutex);
  current[cpu_id]->state = PROCESS_WAITING;
  pthread_mutex_unlock(&current_mutex);
  schedule(cpu_id);
}

/*
 * terminate() is called by the simulator when a process completes.
 * marks the process as terminated, then calls schedule() to select
 * a new process to run on this CPU.
 * args: int - id of CPU process wishing to terminate is currently running on.
 *
 * THIS FUNCTION IS ALREADY COMPLETED - DO NOT MODIFY
 */
extern void terminate(unsigned int cpu_id) {
  // use lock to ensure thread-safe access to current process
  printf("terminate()\n");
  fflush(stdout);
  pthread_mutex_lock(&current_mutex);
  current[cpu_id]->state = PROCESS_TERMINATED;
  pthread_mutex_unlock(&current_mutex);
  schedule(cpu_id);
}

/*
 * wake_up() is called for a new process and when an I/O request completes.
 * The current implementation handles basic FIFO scheduling by simply
 * marking the process as READY, and calling addReadyProcess to put it in the
 * ready queue.  No locks are needed to set the process state as its not
 * possible for anyone else to also access it at the same time as wake_up
 *
 * TO-DO: If the scheduling algorithm is static priority, wake_up() may need
 * to preempt the CPU with the lowest priority process to allow it to
 * execute the process which just woke up.  However, if any CPU is
 * currently running idle, or all of the CPUs are running processes
 * with a higher priority than the one which just woke up, wake_up()
 * should not preempt any CPUs. To preempt a process, use force_preempt().
 * Look in os-sim.h for its prototype and parameters.
 *
 * THIS FUNCTION IS PARTIALLY COMPLETED - REQUIRES MODIFICATION
 */
extern void wake_up(pcb_t* process) {
  printf("Process %s woken up!\n", process->name);
  fflush(stdout);
  if (alg == StaticPriority) {
    for (int id = 0; id < cpu_count; id++) {
      pthread_mutex_lock(&current_mutex);
      if (current[id] == NULL || current[id]->state == PROCESS_WAITING) {
        // If CPU is idling that means ready queue is empty.
        // Adding a process to it will cause that process to run.
        current[id] = process;
        pthread_mutex_unlock(&current_mutex);
        process->state = PROCESS_RUNNING;
        context_switch(id, process, -1);
        // addReadyProcess(process);
        // schedule(id);
        return;

      } else if (current[id]->static_priority < process->static_priority) {
        pthread_mutex_unlock(&current_mutex);
        printf("force_preempt(%d) for %s\n", id, process->name);
        fflush(stdout);

        // Force preempt on this CPU
        force_preempt(id);
        process->state = PROCESS_RUNNING;
        context_switch(id, process, -1);
        return;
      }
      // else loop again checking next CPU
    }
    pthread_mutex_unlock(&current_mutex);
    addReadyProcess(process);
  } else {
    addReadyProcess(process);
  }
}

/* The following 2 functions implement a FIFO ready queue of processes */

/*
 * addReadyProcess adds a process to the end of a pseudo linked list (each
 * process struct contains a pointer next that you can use to chain them
 * together) it takes a pointer to a process as an argument and has no return
 */
static void addReadyProcess(pcb_t* proc) {
  // Ensure no other process can access ready list while we update it
  pthread_mutex_lock(&ready_mutex);

  // if alg is StaticPriority add the processes in order of priority
  if (alg == StaticPriority) {
    pcb_t* current = head;
    if (head == NULL) {  // Empty queue
      printf("adding process %s to empty Q\n", proc->name);
      fflush(stdout);
      head = proc;
      tail = proc;
      // if list was empty may need to wake up idling CPU
      pthread_cond_signal(&ready_empty);
      // printf("signaling to wake up idling CPU\n");
      // fflush(stdout);
    } else if (head->next == NULL) {
      printf("adding process to Q of size 1\n");
      fflush(stdout);
      if (proc->static_priority > head->static_priority) {
        pcb_t* temp = head;
        proc->next = temp;
        head = proc;
        tail = proc->next;
      } else {
        head->next = proc;
        tail = proc;
      }
    } else {
      printReadyQueue();
      printf("adding process to Q of size > 1\n");
      fflush(stdout);
      while (current->next != NULL &&
             current->next->static_priority > proc->static_priority) {
        current = current->next;
      }
      pcb_t* temp = current->next;
      current->next = proc;
      proc->next = temp;
      printReadyQueue();
    }
  } else {
    // add this process to the end of the ready list
    if (head == NULL) {
      head = proc;
      tail = proc;
      // if list was empty may need to wake up idle process
      pthread_cond_signal(&ready_empty);
    } else {
      tail->next = proc;
      tail = proc;
    }

    // ensure that this proc points to NULL
    proc->next = NULL;
  }

  proc->state = PROCESS_READY;
  printf("Process %s on ready queue!\n", proc->name);
  fflush(stdout);
  pthread_mutex_unlock(&ready_mutex);
}

/*
 * getReadyProcess removes a process from the front of a pseudo linked list
 * (each process struct contains a pointer next that you can use to chain them
 * together) it takes no arguments and returns the first process in the ready
 * queue, or NULL if the ready queue is empty
 *
 * TO-DO: handle priority scheduling
 *
 * THIS FUNCTION IS PARTIALLY COMPLETED - REQUIRES MODIFICATION
 */
static pcb_t* getReadyProcess(void) {
  // ensure no other process can access ready list while we update it
  pthread_mutex_lock(&ready_mutex);

  // if list is empty, unlock and return null
  if (head == NULL) {
    pthread_mutex_unlock(&ready_mutex);
    return NULL;
  }

  // get first process to return and update head to point to next process
  pcb_t* first = head;
  head = first->next;

  // if there was no next process, list is now empty, set tail to NULL
  if (head == NULL) tail = NULL;

  pthread_mutex_unlock(&ready_mutex);
  printf("Giving process %s to scheduler\n", first->name);
  fflush(stdout);
  return first;
}

// prints the ready queue for debugging purposes
static void printReadyQueue() {
  // pthread_mutex_lock(&ready_mutex);
  pcb_t* current = head;
  while (current != NULL) {
    printf(" %s ---->", current->name);
    current = current->next;
  }
  printf("\n");
  fflush(stdout);
  // pthread_mutex_unlock(&ready_mutex);
}

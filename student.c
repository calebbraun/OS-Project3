/*
 * student.c
 * This file contains the CPU scheduler for the simulation.
 * original base code from http://www.cc.gatech.edu/~rama/CS2200
 * Last modified 5/11/2016 by Sherri Goings
 *
 * Caleb Braun and Reilly Hallstrom
 * 5/19/2016
 *
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
static void addMultiLevelProcess(pcb_t* proc);
static void updatePriorities(void);
static pcb_t* getMultiLevelProcess(void);

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
  StaticPriority,
  MultiLevel
} scheduler_alg;

scheduler_alg alg;

// declare other global vars
int time_slice = -1;
int cpu_count;
int max_wait_time;

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
  } else if (argc > 2 && strcmp(argv[2], "-m") == 0 && argc > 4) {
    alg = MultiLevel;
    time_slice = atoi(argv[3]);
    max_wait_time = atoi(argv[4]);
    printf("running with multi-level feedback queues\n");
  } else {
    fprintf(stderr,
            "Usage: ./os-sim <# CPUs> [ -r <time slice> | -p | -m <time slice> "
            "<max wait time>]\n"
            "    Default : FIFO Scheduler\n"
            "					-m : Multi level Feedback "
            "Queue "
            "Scheduler\n"
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

  // FIFO and Static Priority don't use time slices
  if (alg == FIFO || alg == StaticPriority) {
    context_switch(cpu_id, proc, -1);
  } else if (alg == RoundRobin) {
    context_switch(cpu_id, proc, time_slice);
  } else if (alg == MultiLevel) {
    updatePriorities();
    context_switch(cpu_id, proc, time_slice);
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
  pthread_mutex_lock(&current_mutex);
  pcb_t* proc = current[cpu_id];
  pthread_mutex_unlock(&current_mutex);

  // Puts the running process on the ready queue
  addReadyProcess(proc);
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
  if (alg == StaticPriority) {
    // Integer to keep track of which CPU is running the lowest priority proc.
    int lowestPrioCPU = 0;

    for (int id = 0; id < cpu_count; id++) {
      pthread_mutex_lock(&current_mutex);

      // If a CPU is idling, just put the process on the ready queue and
      // addReadyProcess() will signal the CPU to run the process
      if (current[id] == NULL) {
        pthread_mutex_unlock(&current_mutex);
        addReadyProcess(process);
        return;
      }
      // If the CPUs are all running, find the CPU with the lowest priority
      else if (current[id]->static_priority <
               current[lowestPrioCPU]->static_priority) {
        pthread_mutex_unlock(&current_mutex);
        lowestPrioCPU = id;
      }
      // else loop again checking next CPU
      pthread_mutex_unlock(&current_mutex);
    }

    // force_preempt() the CPU with the lowest priority process
    if (current[lowestPrioCPU]->static_priority < process->static_priority) {
      pthread_mutex_unlock(&current_mutex);
      addReadyProcess(process);
      force_preempt(lowestPrioCPU);
      return;
    }

    pthread_mutex_unlock(&current_mutex);
  }
  // FIFO and Round Robin (and Static Priority if process is low priority)
  addReadyProcess(process);
}

// gets the next process by first checking the level 1 Q, and so on...
static pcb_t* getMultiLevelProcess(void) {
  // ensure no other process can access ready list while we update it
  pthread_mutex_lock(&ready_mutex);

  pcb_t* first;
  // need to find the first process;
  if (head1 == NULL) {
    if (head2 == NULL) {
      if (head3 == NULL) {
        if (head4 == NULL) {
          pthread_mutex_unlock(&ready_mutex);
          return NULL;
        } else {
          first = head4;
          head4 = first->next;
          first->next = NULL;
        }
      } else {
        first = head3;
        head3 = first->next;
        first->next = NULL;
      }
    } else {
      first = head2;
      head2 = first->next;
      first->next = NULL;
    }
  } else {
    first = head1;
    head1 = first->next;
    first->next = NULL;
  }

  // if there was no next process, list is now empty, set tail to NULL
  if (head1 == NULL) tail1 = NULL;
  if (head2 == NULL) tail2 = NULL;
  if (head3 == NULL) tail3 = NULL;
  if (head4 == NULL) tail4 = NULL;

  pthread_mutex_unlock(&ready_mutex);
  return first;
}

static void addMultiLevelProcess(pcb_t* proc) {
  pthread_mutex_lock(&ready_mutex);
  int prio = proc->priority;
  if (prio == 1) {
    if (head1 == NULL) {
      head1 = proc;
      tail1 = proc;
      // pthread_cond_signal(&ready_empty);
    } else {
      tail1->next = proc;
      tail1 = proc;
    }
  } else if (prio == 2) {
    if (head2 == NULL) {
      head2 = proc;
      tail2 = proc;
      // pthread_cond_signal(&ready_empty);
    } else {
      tail2->next = proc;
      tail2 = proc;
    }
  } else if (prio == 3) {
    if (head3 == NULL) {
      head3 = proc;
      tail3 = proc;
      // pthread_cond_signal(&ready_empty);
    } else {
      tail3->next = proc;
      tail3 = proc;
    }
  } else {
    if (head4 == NULL) {
      head4 = proc;
      tail4 = proc;
      // pthread_cond_signal(&ready_empty);
    } else {
      tail4->next = proc;
      tail4 = proc;
    }
  }
  proc->next = NULL;
  proc->time_added = getSimulatorTime();

  pthread_mutex_unlock(&ready_mutex);
  return;
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

  // if alg is StaticPriority order queue by priority
  if (alg == StaticPriority) {
    pcb_t* current = head;
    // Empty queue
    if (head == NULL) {
      head = proc;
      tail = proc;
      pthread_cond_signal(&ready_empty);  // wake up any idling CPUs
    }
    // One process on queue
    else if (head->next == NULL) {
      if (proc->static_priority > head->static_priority) {
        pcb_t* temp = head;
        proc->next = temp;
        head = proc;
        tail = proc->next;
      } else {
        head->next = proc;
        tail = proc;
      }
    }
    // More than one process on queue
    else {
      while (current->next != NULL &&
             current->next->static_priority > proc->static_priority) {
        current = current->next;
      }
      pcb_t* temp = current->next;
      current->next = proc;
      proc->next = temp;
    }
  } else {  // If not Static Priority, just add process to end of queue
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
  if (alg == MultiLevel) {
    return getMultiLevelProcess();
  }
  // ensure no other process can access ready list while we update it
  pthread_mutex_lock(&ready_mutex);

  // if list is empty, unlock and return null
  if (head == NULL) {
    pthread_mutex_unlock(&ready_mutex);
    return NULL;
  }

  // Get first process to return and update head to point to next process.
  // This
  // is the same for all algorithms because Static Priority has a priority
  // queue
  pcb_t* first = head;
  head = first->next;
  first->next = NULL;

  // if there was no next process, list is now empty, set tail to NULL
  if (head == NULL) tail = NULL;

  pthread_mutex_unlock(&ready_mutex);
  return first;
}

static void updatePriorities(void) {
  pthread_mutex_lock(&ready_mutex);
  unsigned int currentTime = getSimulatorTime();
  pcb_t* currentProc;
  for (size_t i = 1; i < 4; i++) {
    if (i == 1) currentProc = head2;
    if (i == 2) currentProc = head3;
    if (i == 3) currentProc = head4;
    while (currentProc != NULL) {
      if (currentTime - currentProc->time_added > max_wait_time) {
        currentProc->priority = i;
      }
      currentProc = currentProc->next;
    }
  }
  pthread_mutex_unlock(&ready_mutex);
}

////////////////////////////////////////////////////
#ifndef _PSTAT_H
#define _PSTAT_H

enum procstate { UNUSED, USED, SLEEPING, RUNNABLE, RUNNING, ZOMBIE };
///////////////////////////////////////

struct rusage {
   uint cputime;
};
/////////////////////////////////////////////////////
struct pstat {
 int pid; // Process ID
 enum procstate state; // Process state
 uint64 size; // Size of process memory (bytes)
 int ppid; // Parent process ID
 char name[16]; // Parent command name
int priority;           // Scheduling priority
};
///////////////////////////////////////

#endif

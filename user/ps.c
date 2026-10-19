#include "kernel/param.h"
#include "kernel/types.h"
#include "kernel/pstat.h"
#include "user/user.h"

int
main(int argc, char **argv)
{
  struct pstat uproc[NPROC];
  int nprocs;
  int i;
  ////////////////2.3
  int age;
  ///////////////////////2.3
  char *state;
  static char *states[] = {
    [SLEEPING]  "sleeping",
    [RUNNABLE]  "runnable",
    [RUNNING]   "running ",
    [ZOMBIE]    "zombie  "
  };

  nprocs = getprocs(uproc);
  if (nprocs < 0)
    exit(-1);
  ///////////////////////////////////2.2, 2.3
  //  printf("pid\tstate\t\tsize\tppid\tname\n");
  // printf("pid\tstate\t\tsize\tppid\tname\t\tpriority\n");
  printf("pid\tstate\t\tsize\tage\tppid\tname\t\tpriority\n");
  /////////////////////////////2.2, 2.3 
 for (i=0; i<nprocs; i++) {
    state = states[uproc[i].state];

    /////////////////2.3
    // age is the time spent waiting, when RUNNABLE
    if (uproc[i].state == RUNNABLE)
      // if runnable, calculate age
      age = uptime() - uproc[i].readytime;
    else
      // else default to 0
      age = 0;
    ////////////////2.3
    /////////////////////////////////2.2, 2.3
    printf("%d\t%s\t%ld\t%d\t%d\t%s\t\t%d\n", uproc[i].pid, state,
                   uproc[i].size,age, uproc[i].ppid, uproc[i].name,
                 uproc[i].priority );
   // printf("%d\t%s\t%ld\t%d\t%s\t\t%d\n", uproc[i].pid, state,
   //                uproc[i].size, uproc[i].ppid, uproc[i].name,
   //		   uproc[i].priority );
   //  printf("%d\t%s\t%ld\t%d\t%s\n", uproc[i].pid, state,
   //                uproc[i].size, uproc[i].ppid, uproc[i].name);
   /////////////////////////////////2.2, 2.3
 }

  exit(0);
}



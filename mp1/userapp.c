
#include <math.h>

#include "userapp.h"

#define FILENAME "/proc/mp1/status"

#define TIMER 15                       // approximate amount of time for app to last

int main(int argc, char* argv[])
{
	pid_t pid;
	FILE *proc_file;
   int res;
   unsigned long i, j, k;

	pid = getpid();

	proc_file = fopen(FILENAME, "w");

   if (proc_file != NULL) {
      /* write to mp1 file */
      res = fprintf(proc_file, "%d", pid);
      if (res < 0) {
         return res;
      }

      /* close file */
      fclose(proc_file);
   }

   for (i = 0; i < TIMER; i++) {
      /* body takes ~1s to execute */
      k = 1;
      for (j = 1; j < 1<<28; j++) {
         k *= j;
      }
   }

	return 0;
}

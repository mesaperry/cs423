#include "userapp.h"

#define FILENAME "/proc/mp1/status"

int main(int argc, char* argv[])
{
	pid_t pid;
	FILE *proc_file;
   int res;
   unsigned long i, j, k;
	
	pid = getpid();

	proc_file = fopen(FILENAME, "w");
   res = fprintf(proc_file, "%d", pid);
   printf("%d\n", pid);
   
   if (res < 0) {
      return res;
   }

   k = 1;
   for (i = 0; i < 2^32; i++) {
      for (j = 1; j < 2^16; j++) {
         k *= j;
      }
   }

	return 0;
}

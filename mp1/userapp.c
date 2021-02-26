#include "userapp.h"

#define FILENAME "/proc/mp1/status"

int main(int argc, char* argv[])
{
	pid_t pid;
	FILE *proc_file;
   int res;
	
	pid = getpid();

	proc_file = fopen(FILENAME, "w");
   res = fprintf(proc_file, "%d", pid);
   printf("%d\n", pid);
   
   if (res < 0) {
      return res;
   }

	return 0;
}

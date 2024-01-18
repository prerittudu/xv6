#include "types.h"
#include "stat.h"
#include "user.h"
#include "fs.h"

int main (int argc,char *argv[])
{

 int pid;
 int status=-1,a=3,b=4,c=2;
 	
 pid = fork ();
 
 if (pid == 0)
   {	
   	exec(argv[1],argv);
    	printf(1, "exec %s failed\n", argv[1]);
    }
  else
 {
    status=wait2(&a,&b,&c);
 }  
 printf(1, "Wait Time = %d\n Run Time = %d I/O wait time = %d with Status %d \n",a,b,c,status); 
 exit();
 
}

#include "types.h"
#include "stat.h"
#include "user.h"

int main(void) 
{
	void* buff= (char *)malloc(2380);
	draw(buff,2380);
	printf(1, "%s\n", buff);
	
    	exit();
}

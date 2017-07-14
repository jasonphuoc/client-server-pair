#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define WINDOW_SIZE 5120
#define PACKET_SIZE 1024
#define MAX_SEQ 30720
#define TIMEOUT 50

struct packet
{
  int p_type;
  int seq_number;
  int length;

  char content[PACKET_SIZE-sizeof(int)*3];
};

void error(char *msg){
	perror(msg);
	exit(0);
}

int error_gen(double prob){
	int rand_no = (rand() % 100);
	//	printf("Random int: %d\n", rand_int);
	if (100*prob > rand_no)
		return 1;
	else
		return 0;
}


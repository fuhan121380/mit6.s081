#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  
  if(argc < 2) {
    fprintf(2, "input error\n");
    exit(0);
  }
  int cnt = atoi(argv[1]);
  if( sleep(cnt) < 0) {
    fprintf(2, "sleep error");
    exit(0);
  };
  exit(0);
}
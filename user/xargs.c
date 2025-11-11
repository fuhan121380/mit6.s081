#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/param.h"

int
main(int argc, char *argv[])
{
    char ch;
    char buf[MAXARG];
    char *args[MAXARG];
    int index = 0;

    if(argc < 2) {
        fprintf(2, "input error");
        exit(0);
    }
    while(read(0, &ch, 1) > 0) {
        if(ch == '\n')
        {
            int args_cnt = 0;
            if(index < MAXARG - 1) {
                buf[index++] = '\0';
            }
            for(int i = 1; i < argc; i++) {
                args[args_cnt++] = argv[i];
            }
            if(args_cnt < MAXARG - 1) {
                args[args_cnt++] = buf;
            }
            if(fork() == 0) {
                exec(argv[1], args);
            }
            wait(0);
            index = 0;
        } else {
            if(index < MAXARG - 1) {
                buf[index++] = ch;
            }
        }
    }
    exit(0);
}
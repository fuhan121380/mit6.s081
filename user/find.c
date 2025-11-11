#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"

char*
fmtname(char *path)
{
  static char buf[DIRSIZ+1];
  char *p;

  // Find first character after last slash.
  for(p=path+strlen(path); p >= path && *p != '/'; p--)
    ;
  p++;

  // Return blank-padded name.
  if(strlen(p) >= DIRSIZ)
    return p;
  memmove(buf, p, strlen(p));
  buf[strlen(p)] = '\0';
  return buf;
}

void
find(char *path, char *target) {
    char buf[512], *p;
    int fd;
    struct dirent de;
    struct stat st;

    if((fd = open(path, 0)) < 0){
        fprintf(2, "ls: cannot open %s\n", path);
        return;
    }

    if(fstat(fd, &st) < 0) {
        fprintf(2, "ls: cannot stat %s\n", path);
        close(fd);
        return;
    }

    if(strlen(path) + 1 + DIRSIZ + 1 > sizeof buf){
        printf("ls: path too long\n");
        close(fd);
        return;
    }

    strcpy(buf, path);
    p = buf + strlen(buf);
    *p++ = '/';
    //printf("%s\n", buf);

    while (read(fd, &de, sizeof(de)) == sizeof(de))
    {
        if(de.inum == 0)
            continue;
        memmove(p, de.name, DIRSIZ);
        p[DIRSIZ] = 0;
        
        if(stat(buf, &st) < 0){
            printf("find: cannot stat %s\n", buf);
            continue;
        }

        switch(st.type) {
            case T_FILE:
                //printf("%s\n", fmtname(buf));
                //printf("%s\n", target);
                //printf("%s\n", buf);
                if(strcmp(target, fmtname(buf)) == 0) {
                    printf("%s\n", buf);
                }
                break;
            case T_DIR:
                if(strcmp(de.name, ".") != 0 && strcmp(de.name, "..") != 0) {
                    find(buf, target);
                }
                break;
        }
    } 
}

int
main(int argc, char *argv[])
{
  if(argc < 2){
    fprintf(2, "input error\n");
    exit(0);
  }
  find(argv[1], argv[2]);
  exit(0);
}

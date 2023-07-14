#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"


// recusively find file_name in path
// path is directory by default
void find(char* path, char* file_name)
{
    char buf[512], *p;
    int fd; // file descriptor
    struct stat st;
    struct dirent de;
    if ((fd = open(path, 0)) < 0)
    {
        fprintf(2, "find: cannot open %s\n", path);
        return;
    }
    if (fstat(fd, &st) < 0)
    {
        fprintf(2, "find: cannot open %s\n", path);
        close(fd);
        return;
    }
    if(st.type == T_FILE)
    {
        fprintf(2, "find: please enter directory path\n");
        close(fd);
        return;
    }
    if(strlen(path) + 1 + DIRSIZ + 1 > sizeof buf)
    {
        printf("find: path too long\n");
        close(fd);
        return;
    }
    strcpy(buf, path);
    p = buf + strlen(buf);
    *p++ = '/';
    // read from directory，every time read a dirent
    while (read(fd, &de, sizeof(de)) == sizeof(de))
    {
        if((de.inum == 0) || (strcmp(de.name, ".") == 0) || (strcmp(de.name, "..") == 0))
            continue;
        memmove(p, de.name, DIRSIZ);
        p[DIRSIZ] = 0; // add end mark
        if(stat(buf, &st) < 0)
        {
            printf("ls: cannot stat %s\n", buf);
            continue;
        }
        if (st.type == T_DIR)
        {
            // if path is a directory, continue to recursion
            find(buf, file_name);
        }
        else 
        {
            // if path is a file, then compare it with file name
            if (strcmp(de.name, file_name) == 0)
            {
                fprintf(1, "%s\n", buf);
                continue;
            }
        }
    }
    close(fd);
    return;
    // exit(0);
}


int main(int argc, char *argv[])
{
    if (argc < 3) 
    {
        fprintf(2, "find: please enter enough arguments\n");
        exit(1);
    }
    if (argc > 3)
    {
        fprintf(2, "find: too many arguments\n");
        exit(1);
    }
    find(argv[1], argv[2]);
    exit(0);
}
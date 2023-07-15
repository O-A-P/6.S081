#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/param.h"

int main(int argc, char *argv[])
{
    char *argv_real[MAXARG];
    char tmp = 0;
    char arg[MAXARG]; // store arg passed by pipe
    int index = 0;

    if (argc < 2)
    {
        fprintf(2, "xargs: more arguments required!");
    }
    if (argc > MAXARG - 1)
    {
        fprintf(2, "xargs: too many arguments!");
    }

    for (int i = 0; i < argc - 1; i++)
    {
        argv_real[i] = argv[i + 1];
    }

    while (read(0, &tmp, sizeof(tmp)) != 0)
    {
        if (index == MAXARG - 1)
        {
            fprintf(2, "xargs: argument is too long");
        }
        if (tmp != '\n')
        {
            arg[index++] = tmp;
        }
        else
        {
            arg[index] = '\0';
            index = 0;
            // fork and exec
            if (fork() == 0)
            {
                argv_real[argc - 1] = arg;
                exec(argv_real[0], argv_real);
                exit(0);
            }
            else
            {
                // wait for child process
                wait(0);
            }
        }
    }
    exit(0);
}

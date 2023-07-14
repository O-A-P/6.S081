#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

void prime(int read_pipe, int write_pipe, int divider)
{
    int num;
    while (read(read_pipe, &num, sizeof(int)) != 0)
    {
        if (num % divider != 0)
        {
            write(write_pipe, &num, sizeof(int));
        }
    }
    close(read_pipe);
    close(write_pipe);
    
    exit(0);
}

#define MAX 35

int main()
{
    int p[2];
    pipe(p);

    if (fork() == 0)
    {
        close(p[0]);
        // child process 0: write all number to pipe
        for (int i = 2; i <= MAX; i++)
        {
            write(p[1], &i, sizeof(int));
        }
        close(p[1]);
        exit(0);
    }

    int num;
    int read_pipe = p[0];
    int write_pipe;
    close(p[1]);

    while (read(read_pipe, &num, sizeof(int)) != 0)
    {
        int p_next[2];
        pipe(p_next);
        write_pipe = p_next[1];

        if (fork() == 0)
        {
            // child process
            printf("prime %d\n", num);
            // read data and send prime to the next pipe
            prime(read_pipe, write_pipe, num);
            close(read_pipe);
            close(write_pipe);
            close(p_next[0]);
            // close this process
            exit(0);
        }
        else
        {
            // father process
            close(read_pipe);
            close(write_pipe);
            read_pipe = p_next[0];
        }
    }
    // father: close read end
    close(read_pipe);
    wait(0);
    exit(0);
}

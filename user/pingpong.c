#include "kernel/types.h"
#include "user/user.h" // fork

int main(int argc, char *argv[])
{
    char buffer[5];
    if (argc > 1)
    {
        fprintf(2, "pingpong: too many arguments");
    }
    int parent_to_child[2];
    int child_to_parent[2];
    // trans data from parent to child
    pipe(parent_to_child);
    // trans data from child to parent
    pipe(child_to_parent);

    if (fork() == 0)
    {
        // child process
        // close write end of parent to child;
        close(parent_to_child[1]);
        // close read en of child to parent;
        close(child_to_parent[0]);
        write(child_to_parent[1], "pong\n", 5);
        // close write end of child to parent
        close(child_to_parent[1]);
        read(parent_to_child[0], buffer, sizeof(buffer));
        // close read end of parent to child
        close(parent_to_child[1]);
        printf("%d: received ", getpid());
        write(1, buffer, 5);
    }
    else
    {
        // parent process
        // close read end of parent to child
        close(parent_to_child[0]);
        // close write end of child to parent
        close(child_to_parent[1]);
        write(parent_to_child[1], "ping\n", 5);
        // close write end of parent to child
        close(parent_to_child[1]);
        read(child_to_parent[0], buffer, sizeof(buffer));
        // close read end of child to parent
        close(child_to_parent[0]);
        printf("%d: received ", getpid());
        write(1, buffer, 5);
    }
    exit(0);
}

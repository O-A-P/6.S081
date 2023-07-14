#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int atoi(const char *s);

int main(int argc, char *argv[])
{
    if (argc <= 1)
    {
        fprintf(2, "sleep: please set up sleep time\n");
        exit(1);
    }
    if (argc > 2)
    {
        fprintf(2, "sleep: too many arguments\n");
        exit(1);
    }
    sleep(atoi(argv[1]));
    exit(0);
}

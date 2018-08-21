#include "stdafx.h"

int fcntl (int __fd, int __cmd, ...)
{
    printf("in hooked fcntl.\n");
    return 42;
}
#include "stdafx.h"

MockFileSystem *g_mock_fs = nullptr;
MockSystemTime *g_mock_st = nullptr;

int fcntl (int __fd, int __cmd, ...)
{
    if (F_GETFD == __cmd || F_GETFL == __cmd) {
        return g_mock_fs->get_flag(__fd);
    } else if (F_SETFD == __cmd || F_SETFL == __cmd) {
        va_list val;
        va_start(val, __cmd);
        int flag = va_arg(val, int);
        va_end(val);
        return g_mock_fs->set_flag(__fd, flag);
    } else {
        return 0;
    }
}
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
    }
    return 0;
}

int gettimeofday (struct timeval *__tv, __timezone_ptr_t __tz) __THROW
{
    if (g_mock_st) {
        *__tv = g_mock_st->m_curr_time;
        g_mock_st->add_interval();
    } else {
        typedef int (*gettime_pfn_t)(struct timeval *__tv, __timezone_ptr_t __tz);
        static gettime_pfn_t g_sys_gettime = (gettime_pfn_t)dlsym(RTLD_NEXT, "gettimeofday");
        g_sys_gettime(__tv, __tz);
    }
    return 0;
}
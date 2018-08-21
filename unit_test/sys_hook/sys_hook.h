#pragma once

#include "stdafx.h"

class MockFileSystem : public testing::Test
{
public:
    int get_flag(int fd)
    {
        auto it = m_fd_flags.find(fd);
        if (m_fd_flags.end() == it)
            return -1;
        else
            return it->second;
    }

    int set_flag(int fd, int flag)
    {
        auto it = m_fd_flags.find(fd);
        if (m_fd_flags.end() == it)
            return -1;

        it->second = flag;
        return 0;
    }

protected:
    std::map<int, int> m_fd_flags;  //file desc/status flags
};

extern MockFileSystem *g_mock_fs;

class MockSystemTime : public testing::Test
{
public:
    void add_interval(int sec, int usec)
    {
        m_seed.tv_sec += sec;
        m_seed.tv_usec += usec;
    }

    timeval get_time()
    {
        return m_seed;
    }

protected:
    timeval m_seed;
};

extern MockSystemTime *g_mock_st;
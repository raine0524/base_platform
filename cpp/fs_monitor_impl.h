#pragma once

#include "stdafx.h"

namespace crx
{
    struct monitor_ev
    {
        bool recur_flag;
        int watch_id;
        uint32_t mask;
        std::string path;
    };

    class fs_monitor_impl : public eth_event
    {
    public:
        void fs_monitory_callback(uint32_t events);

        void recursive_monitor(const std::string& root_dir, bool add, uint32_t mask);

        void trigger_event(bool add, int watch_id, const std::string& path, bool recur_flag, uint32_t mask);

        struct stat m_st;
        std::string m_nfy_buf;

        std::map<std::string, std::shared_ptr<monitor_ev>> m_path_mev;
        std::map<int, std::shared_ptr<monitor_ev>> m_wd_mev;
        std::function<void(const char*, uint32_t)> m_monitor_f;
    };
}

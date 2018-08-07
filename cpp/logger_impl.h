#pragma once

#include "stdafx.h"

namespace crx
{
    //log_impl继承于eth_event使得这个实例可以存放在m_ev_array数组中，但不会添加该文件描述符上的监听事件
    class log_impl : public eth_event
    {
    public:
        log_impl()
        :m_split_idx(0)
        ,m_detach(false)
        ,m_last_sec(-1)
        ,m_fmt_buf(1024, 0)
        ,m_log_buf(65536, 0)
        ,m_fp(nullptr)
        ,m_seria(true)
        ,m_log_idx(0)
        {
            bzero(&m_cmd, sizeof(m_cmd));
        }

        virtual ~log_impl()
        {
            if (m_fp)
                fclose(m_fp);
        }

        std::string get_log_path(bool create);

        void create_log_file(const char *log_file);

        void rotate_log(bool create_dir);

        void get_local_log(std::shared_ptr<scheduler_impl>& sch_impl);

        void write_local_log(const char *data, size_t len);

        void get_remote_log(std::shared_ptr<scheduler_impl>& sch_impl);

        void flush_log_buffer();

        std::string m_prefix, m_root_dir;
        int64_t m_max_size, m_curr_size, m_split_idx;
        std::shared_ptr<simpack_server_impl> m_simp_impl;

        bool m_detach;
        datetime m_now;
        int64_t m_last_sec, m_last_hour, m_last_day;
        std::string m_fmt_buf, m_fmt_tmp, m_log_buf;
        FILE *m_fp;

        server_cmd m_cmd;
        seria m_seria;
        uint32_t m_log_idx;
        timer_wheel m_sec_wheel;
    };
}
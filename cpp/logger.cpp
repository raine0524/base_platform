#include "stdafx.h"

namespace crx
{
    log scheduler::get_log(const char *prefix, const char *root_dir /*= "log_files"*/, int max_size /*= 10*/)
    {
        crx::log ins;
        auto impl = std::make_shared<log_impl>();
        ins.m_impl = impl;

        impl->m_prefix = prefix;
        impl->m_root_dir = root_dir;
        auto sch_impl = std::dynamic_pointer_cast<scheduler_impl>(m_impl);
        impl->sch_impl = sch_impl;
        if (!sch_impl->m_remote_log)
            impl->m_max_size = max_size*1024*1024;
        else
            impl->m_max_size = max_size;

        if (sch_impl->m_remote_log)
            impl->get_remote_log(sch_impl);
        return ins;
    }

    void log::printf(const char *fmt, ...)
    {
        if (!m_impl) {
            va_list val;
            va_start(val, fmt);
            vprintf(fmt, val);
            va_end(val);
            return;
        }

        timeval tv = {0};
        gettimeofday(&tv, nullptr);

        //格式化
        auto impl = std::dynamic_pointer_cast<log_impl>(m_impl);
        if (!impl->m_fp) {
            auto sch_impl = impl->sch_impl.lock();
            impl->get_local_log(sch_impl);
            if (!impl->m_fp) return;

            int fd = fileno(impl->m_fp);        //使用本地日志时需要周期性的刷新缓冲，因此在内部需要保存一个该实例的指针
            if (fd >= sch_impl->m_ev_array.size())
                sch_impl->m_ev_array.resize((size_t)fd+1);
            sch_impl->m_ev_array[fd] = impl;
        }

        if (tv.tv_sec != impl->m_last_sec) {
            impl->m_last_sec = tv.tv_sec;
            impl->m_now = get_datetime(&tv);
            sprintf(&impl->m_fmt_buf[0], "[%04d/%02d/%02d %02d:%02d:%02d.%06ld] ", impl->m_now.t->tm_year+1900, impl->m_now.t->tm_mon+1,
                    impl->m_now.t->tm_mday, impl->m_now.t->tm_hour, impl->m_now.t->tm_min, impl->m_now.t->tm_sec, tv.tv_usec);
        } else {
            sprintf(&impl->m_fmt_buf[21], "%06ld", tv.tv_usec);
            impl->m_fmt_buf[27] = ']';
        }

        va_list var1, var2;
        va_start(var1, fmt);
        va_copy(var2, var1);

        char *data = &impl->m_fmt_buf[0];
        size_t remain = impl->m_fmt_buf.size()-29;
        size_t ret = (size_t)vsnprintf(&impl->m_fmt_buf[29], remain, fmt, var1);
        if (ret > remain) {
            impl->m_fmt_tmp.resize(ret+30, 0);
            data = &impl->m_fmt_tmp[0];
            strncpy(&impl->m_fmt_tmp[0], impl->m_fmt_buf.c_str(), 29);
            ret = (size_t)vsnprintf(&impl->m_fmt_tmp[29], ret+1, fmt, var2);
        }

        va_end(var2);
        va_end(var1);

        ret += 29;
        if (impl->m_log_idx) {      //写远程日志
            impl->m_doc.SetObject();
            Document::AllocatorType& alloc = impl->m_doc.GetAllocator();
            impl->m_doc.AddMember("log_idx", impl->m_log_idx, alloc);
            impl->m_doc.AddMember("data", Value().SetString(data, (unsigned)ret), alloc);
            impl->m_doc.Accept(impl->m_writer);

            impl->m_write_buf.append_zero();
            const char *buf = impl->m_write_buf.GetString();
            size_t len = impl->m_write_buf.GetSize();

            auto header = (simp_header*)buf;
            header->cmd = impl->m_cmd.cmd = 1;
            header->type = impl->m_cmd.type = 3;    //notify
            header->length = (uint32_t)(len-sizeof(simp_header));

            if (-1 != impl->m_simp_impl->m_log_conn)
                impl->m_simp_impl->send_data(3, impl->m_simp_impl->m_log_conn, impl->m_cmd, buf, len);
            else
                impl->m_simp_impl->m_log_cache.append(buf, len);

            impl->m_write_buf.reset();
            impl->m_writer.Reset(impl->m_write_buf);
        } else {        //本地
            impl->write_local_log(data, ret);
        }
    }

    void log::detach()
    {
        auto impl = std::dynamic_pointer_cast<log_impl>(m_impl);
        auto sch_impl = impl->sch_impl.lock();
        if (!sch_impl->m_remote_log && impl->m_fp) {      //本地日志才会定时刷缓冲并且在内部保留一份实例
            fflush(impl->m_fp);     //首先刷新缓存
            impl->m_detach = true;
        }
    }

    std::string log_impl::get_log_path(bool create)
    {
        m_now = get_datetime();
        m_last_day = m_now.t->tm_mday;
        m_last_hour = m_now.t->tm_hour;

        int ret = 0;
        std::string log_path(256, 0);
        auto pos = m_root_dir.find('@');        //a trick for local log & remote log
        if (std::string::npos != pos)
            ret = sprintf(&log_path[0], "%s/%d/%02d/%02d/%s/", m_root_dir.substr(0, pos).c_str(),
                          m_now.t->tm_year+1900, m_now.t->tm_mon+1, m_now.t->tm_mday, &m_root_dir[pos+1]);
        else
            ret = sprintf(&log_path[0], "%s/%d/%02d/%02d/", m_root_dir.c_str(), m_now.t->tm_year+1900,
                          m_now.t->tm_mon+1, m_now.t->tm_mday);
        log_path.resize((size_t)ret);
        if (create)
            mkdir_multi(log_path.c_str());
        return log_path;
    }

    bool log_impl::create_log_file(const char *log_file)
    {
        if (m_fp) fclose(m_fp);
        m_fp = fopen(log_file, "a");
        if (!m_fp) return false;

        //使用自定义的日志缓冲，大小为64k
        setvbuf(m_fp, &m_log_buf[0], _IOFBF, m_log_buf.size());
        m_curr_size = (int)ftell(m_fp);
        return true;
    }

    void log_impl::get_local_log(std::shared_ptr<scheduler_impl>& sch_impl)
    {
        std::string log_path = get_log_path(true);
        size_t path_size = log_path.size();
        log_path.resize(256, 0);

        char file_wh[64] = {0};
        sprintf(file_wh, "%s_%02d", m_prefix.c_str(), m_now.t->tm_hour);
        depth_first_traverse_dir(log_path.c_str(), [&](const std::string& fname) {
            if (std::string::npos != fname.find(file_wh)) {
                auto pos = fname.rfind('-');
                int split_idx = atoi(&fname[pos+1]);
                if (split_idx > m_split_idx)
                    m_split_idx = split_idx;
            }
        }, false);
        sprintf(&log_path[0]+path_size, "%s-%ld.log", file_wh, m_split_idx);
        if (!create_log_file(log_path.c_str()))
            return;

        if (!m_wheel.m_impl)
            m_wheel = sch_impl->m_wheel;

        //每隔3秒刷一次缓冲
        m_wheel.add_handler(3*1000, std::bind(&log_impl::flush_log_buffer, this));
    }

    void log_impl::get_remote_log(std::shared_ptr<scheduler_impl>& sch_impl)
    {
        auto& impl = sch_impl->m_util_impls[SIMP_SVR];
        if (!impl) {
            printf("远程日志基于 simpack_server 组件，请先实例化该对象\n");
            return;
        }

        m_doc.SetObject();
        Document::AllocatorType& alloc = m_doc.GetAllocator();
        m_doc.AddMember("prefix", Value().SetString(m_prefix.c_str(), (unsigned)m_prefix.size()), alloc);
        m_log_idx = ++sch_impl->m_log_idx;
        m_doc.AddMember("log_idx", m_log_idx, alloc);
        m_doc.AddMember("max_size", m_max_size, alloc);
        m_doc.Accept(m_writer);

        m_write_buf.append_zero();
        const char *data = m_write_buf.GetString();
        size_t len = m_write_buf.GetSize();

        auto header = (simp_header*)data;
        header->cmd = m_cmd.cmd = 1;
        header->type = m_cmd.type = 1;     //request
        header->length = (uint32_t)(len-sizeof(simp_header));

        auto simp_impl = std::dynamic_pointer_cast<simpack_server_impl>(impl);
        simp_impl->m_log_req = std::string(data, len);
        if (-1 != simp_impl->m_log_conn)
            simp_impl->send_data(1, simp_impl->m_log_conn, m_cmd, data, len);
        else
            simp_impl->m_log_cache.append(data, len);
        m_write_buf.reset();
        m_writer.Reset(m_write_buf);
        m_simp_impl = simp_impl;
    }

    void log_impl::rotate_log(bool create_dir)
    {
        std::string log_path = get_log_path(create_dir);
        size_t path_size = log_path.size();
        log_path.resize(256, 0);
        sprintf(&log_path[0]+path_size, "%s_%02ld-%ld.log", m_prefix.c_str(), m_last_hour, m_split_idx);

        int old_fd = fileno(m_fp);
        if (create_log_file(log_path.c_str()))
            return;

        int new_fd = fileno(m_fp);
        if (old_fd != new_fd) {
            auto impl = sch_impl.lock();
            impl->m_ev_array[new_fd] = std::move(impl->m_ev_array[old_fd]);
        }
    }

    void log_impl::write_local_log(const char *data, size_t len)
    {
        int this_hour = atoi(&data[12]);
        if (this_hour != m_last_hour) {     //时钟更新，创建新的日志文件
            m_last_hour = this_hour;
            m_split_idx = 0;

            bool create_dir = false;
            int this_day = atoi(&data[9]);
            if (this_day != m_last_day) {       //日期更新，创建新的目录
                m_last_day = this_day;
                create_dir = true;
            }
            rotate_log(create_dir);
        }

        if (m_fp) {
            fwrite(data, sizeof(char), len, m_fp);
            m_curr_size += len;
        }

        if (m_curr_size >= m_max_size) {    //当前日志文件已超过最大尺寸，创建新的日志文件
            m_split_idx++;
            rotate_log(false);
        }
    }

    void log_impl::flush_log_buffer()
    {
        if (!m_fp) return;
        if (m_detach) {
            auto impl = sch_impl.lock();
            int fd = fileno(m_fp);
            if (0 <= fd && fd < impl->m_ev_array.size() && impl->m_ev_array[fd])
                impl->m_ev_array[fd].reset();
        } else {
            fflush(m_fp);       //周期性的刷日志
            m_wheel.add_handler(3*1000, std::bind(&log_impl::flush_log_buffer, this));
        }
    }
}
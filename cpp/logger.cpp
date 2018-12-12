#include "stdafx.h"

namespace crx
{
    std::shared_ptr<logger_impl> scheduler_impl::get_logger(const char *prefix)
    {
        auto impl = std::make_shared<logger_impl>();
        impl->m_log_file = m_log_root+"/"+prefix+".log";
        impl->m_sch_impl = this;
        return impl;
    }

    void logger::printf(LOG_LEVEL level, const char *fmt, ...)
    {
        if (!m_impl) return;
        auto impl = std::dynamic_pointer_cast<logger_impl>(m_impl);
        if (!impl->m_fp)
            impl->init_logger();

        if (impl->m_now.date != impl->m_last_date)      //日期更新，创建新的日志文件
            impl->rotate_log();

        timeval tv = {0};
        gettimeofday(&tv, nullptr);
        if (tv.tv_sec != impl->m_last_sec) {
            impl->m_last_sec = tv.tv_sec;
            impl->m_now = get_datetime(&tv);
            sprintf(&impl->m_fmt_buf[0], "%04d/%02d/%02d %02d:%02d:%02d ", impl->m_now.t->tm_year+1900, impl->m_now.t->tm_mon+1,
                    impl->m_now.t->tm_mday, impl->m_now.t->tm_hour, impl->m_now.t->tm_min, impl->m_now.t->tm_sec);
        }

        va_list var1, var2;
        va_start(var1, fmt);
        va_copy(var2, var1);

        char *data = &impl->m_fmt_buf[0];
        size_t remain = impl->m_fmt_buf.size()-20;
        size_t ret = (size_t)vsnprintf(&impl->m_fmt_buf[20], remain, fmt, var1);
        if (ret > remain) {
            impl->m_fmt_tmp.resize(ret+21, 0);
            data = &impl->m_fmt_tmp[0];
            strncpy(&impl->m_fmt_tmp[0], impl->m_fmt_buf.c_str(), 20);
            ret = (size_t)vsnprintf(&impl->m_fmt_tmp[20], ret+1, fmt, var2);
        }

        va_end(var2);
        va_end(var1);

        ret += 20;
        if (impl->m_fp)
            fwrite(data, sizeof(char), ret, impl->m_fp);
    }

    void logger_impl::init_logger()
    {
        m_now = get_datetime();
        m_last_date = m_now.date;

        m_fp = fopen(m_log_file.c_str(), "a");
        setvbuf(m_fp, &m_log_buf[0], _IOFBF, m_log_buf.size());

        //每隔3秒刷一次缓冲
        m_sch_impl->m_wheel.add_handler(3*1000, std::bind(&logger_impl::flush_log_buffer, this));
    }

    void logger_impl::rotate_log()
    {
        fclose(m_fp);
        char tmp_buf[64] = {0};
        int64_t year = m_last_date/10000, month = (m_last_date%10000)/100, day = m_last_date%100;
        sprintf(tmp_buf, "%04ld-%02ld-%02ld", year, month, day);

        std::string new_file = m_log_file+"."+tmp_buf;
        rename(m_log_file.c_str(), new_file.c_str());
        m_last_date = m_now.date;

        m_fp = fopen(m_log_file.c_str(), "a");
        setvbuf(m_fp, &m_log_buf[0], _IOFBF, m_log_buf.size());
    }

    void logger_impl::flush_log_buffer()
    {
        fflush(m_fp);       //周期性的刷日志
        m_sch_impl->m_wheel.add_handler(3*1000, std::bind(&logger_impl::flush_log_buffer, this));
    }
}

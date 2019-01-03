#include "stdafx.h"

namespace crx
{
    std::shared_ptr<logger_impl> scheduler_impl::get_logger(const char *prefix)
    {
        auto impl = std::make_shared<logger_impl>();
        impl->m_prefix = prefix;
        impl->m_log_file = m_log_root+"/"+prefix+".log";
        impl->m_sch_impl = this;
        return impl;
    }

    void logger::printf(LOG_LEVEL level, const char *fmt, ...)
    {
        if (!m_impl) return;
        auto impl = std::dynamic_pointer_cast<logger_impl>(m_impl);
        if (level < impl->m_sch_impl->m_log_lvl)
            return;

        if (!impl->m_fp)
            impl->init_logger();

        timeval tv = {0};
        gettimeofday(&tv, nullptr);
        if (tv.tv_sec != impl->m_last_sec) {
            impl->m_last_sec = tv.tv_sec;
            impl->m_now = get_datetime(&tv);
            sprintf(&impl->m_fmt_buf[0], "%04d/%02d/%02d %02d:%02d:%02d ", impl->m_now.t->tm_year+1900, impl->m_now.t->tm_mon+1,
                    impl->m_now.t->tm_mday, impl->m_now.t->tm_hour, impl->m_now.t->tm_min, impl->m_now.t->tm_sec);

            if (impl->m_now.date != impl->m_last_date)      //日期更新，创建新的日志文件
                impl->rotate_log();
        }

        int ts_len = 20, tag_len;
        switch (level) {
            case LVL_DEBUG:
                strcpy(&impl->m_fmt_buf[ts_len], "[DEBUG] ");
                tag_len = 28;
                break;
            case LVL_INFO:
                strcpy(&impl->m_fmt_buf[ts_len], "[INFO] ");
                tag_len = 27;
                break;
            case LVL_WARN:
                strcpy(&impl->m_fmt_buf[ts_len], "[WARN] ");
                tag_len = 27;
                break;
            case LVL_ERROR:
                strcpy(&impl->m_fmt_buf[ts_len], "[ERROR] ");
                tag_len = 28;
                break;
            case LVL_FATAL:
                strcpy(&impl->m_fmt_buf[ts_len], "[FATAL] ");
                tag_len = 28;
                break;
            default:
                return;
        }

        va_list var1, var2;
        va_start(var1, fmt);
        va_copy(var2, var1);

        char *data = &impl->m_fmt_buf[0];
        size_t remain = impl->m_fmt_buf.size()-tag_len;
        size_t ret = (size_t)vsnprintf(&impl->m_fmt_buf[tag_len], remain, fmt, var1);
        if (ret > remain) {
            impl->m_fmt_tmp.resize(ret+tag_len+1, 0);
            data = &impl->m_fmt_tmp[0];
            strncpy(&impl->m_fmt_tmp[0], impl->m_fmt_buf.c_str(), (size_t)tag_len);
            ret = (size_t)vsnprintf(&impl->m_fmt_tmp[tag_len], ret+1, fmt, var2);
        }

        va_end(var2);
        va_end(var1);

        ret += tag_len;
        if (impl->m_fp)
            fwrite(data, sizeof(char), ret, impl->m_fp);

        if (LVL_FATAL == level) {
            fprintf(stderr, "%s", data);
            fflush(impl->m_fp);
            exit(EXIT_FAILURE);
        }
    }

    void logger_impl::init_logger()
    {
        m_now = get_datetime();
        // 日志文件存在且大小非0
        if (!access(m_log_file.c_str(), F_OK) && get_file_size(m_log_file.c_str())) {
            char date_buf[16] = {0};
            m_fp = fopen(m_log_file.c_str(), "a+");
            fseek(m_fp, 0, SEEK_SET);
            fgets(date_buf, sizeof(date_buf), m_fp);

            int year = atoi(&date_buf[0]);
            int month = atoi(&date_buf[5]);
            int day = atoi(&date_buf[8]);
            m_last_date = year*10000+month*100+day;

            if (m_last_date != m_now.date) {        //日期更新，切割日志
                rotate_log();
            } else {        //日期未变，在文件尾部继续追加日志
                fseek(m_fp, 0, SEEK_END);
            }
        } else {
            m_fp = fopen(m_log_file.c_str(), "a");
            m_last_date = m_now.date;
        }

        setvbuf(m_fp, &m_log_buf[0], _IOFBF, m_log_buf.size());
        m_sch_impl->m_wheel.add_handler(3*1000, std::bind(&logger_impl::flush_log_buffer, this));   //每隔3秒刷一次缓冲
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

        std::vector<std::string> backup_files;
        depth_first_traverse_dir(m_sch_impl->m_log_root.c_str(), [&](const std::string& fname) {
            if (std::string::npos != fname.find(m_prefix))
                backup_files.push_back(fname);
        });

        int64_t remove_cnt = backup_files.size()-m_sch_impl->m_back_cnt;
        if (remove_cnt > 0) {
            std::sort(backup_files.begin(), backup_files.end(), std::less<std::string>());
            for (int i = 0; i < remove_cnt; i++)
                remove(backup_files[i].c_str());
        }

        m_fp = fopen(m_log_file.c_str(), "a");
        setvbuf(m_fp, &m_log_buf[0], _IOFBF, m_log_buf.size());
    }

    void logger_impl::flush_log_buffer()
    {
        fflush(m_fp);       //周期性的刷日志
        m_sch_impl->m_wheel.add_handler(3*1000, std::bind(&logger_impl::flush_log_buffer, this));
    }
}

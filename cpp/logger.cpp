#include "stdafx.h"

namespace crx
{
    std::shared_ptr<logger_impl> scheduler_impl::get_logger(scheduler *sch, const char *prefix, int logger_cmd)
    {
        auto impl = std::make_shared<logger_impl>();
        impl->m_prefix = prefix;
        impl->m_log_file = m_log_root+"/"+prefix+".log";
        impl->m_sch_impl = this;
        impl->m_logger_cmd = logger_cmd;
        if (!m_log_ev.m_impl)
            m_log_ev = sch->get_event(std::bind(&logger_impl::write_logger_str, impl.get()));
        return impl;
    }

    void logger::printf(LOG_LEVEL level, const char *fmt, ...)
    {
        if (!m_impl) return;
        auto impl = std::dynamic_pointer_cast<logger_impl>(m_impl);
        if (level < impl->m_sch_impl->m_log_lvl)
            return;

        int tag_len;
        std::string fmt_buf;
        switch (level) {
            case LVL_DEBUG:
                fmt_buf = "[DEBUG] ";
                tag_len = 8;
                break;
            case LVL_INFO:
                fmt_buf = "[INFO] ";
                tag_len = 7;
                break;
            case LVL_WARN:
                fmt_buf = "[WARN] ";
                tag_len = 7;
                break;
            case LVL_ERROR:
                fmt_buf = "[ERROR] ";
                tag_len = 8;
                break;
            case LVL_FATAL:
                fmt_buf = "[FATAL] ";
                tag_len = 8;
                break;
            default:
                return;
        }

        va_list var1, var2;
        va_start(var1, fmt);
        va_copy(var2, var1);

        fmt_buf.resize((size_t)(1+tag_len+vsnprintf(nullptr, 0, fmt, var1)));
        vsnprintf(&fmt_buf[tag_len], fmt_buf.size()-tag_len, fmt, var2);

        va_end(var2);
        va_end(var1);

        logger_cmd cmd;
        cmd.cmd = impl->m_logger_cmd;
        cmd.logger_str = std::move(fmt_buf);

        {
            std::lock_guard<std::mutex> lck(impl->m_sch_impl->m_mtx);
            impl->m_sch_impl->m_logger_strs.push_back(std::move(cmd));
        }
        impl->m_sch_impl->m_log_ev.notify();
    }

    void logger_impl::write_logger_str()
    {
        std::vector<logger_cmd> logger_cmds;
        {
            std::lock_guard<std::mutex> lck(m_sch_impl->m_mtx);
            logger_cmds = std::move(m_sch_impl->m_logger_strs);
        }

        timeval tv = {0};
        std::string fatal_str;
        std::shared_ptr<logger_impl> impl;

        for (auto& cmd : logger_cmds) {
            if (1 == cmd.cmd)           // 1-lib log
                impl = std::dynamic_pointer_cast<logger_impl>(g_lib_log.m_impl);
            else if (2 == cmd.cmd)      // 2-app log
                impl = std::dynamic_pointer_cast<logger_impl>(g_app_log.m_impl);
            else        // unknown log
                continue;

            if (!impl->m_fp)
                init_logger();

            gettimeofday(&tv, nullptr);
            if (tv.tv_sec != impl->m_last_sec) {
                impl->m_last_sec = tv.tv_sec;
                impl->m_now = get_datetime(&tv);
                sprintf(&impl->m_fmt_buf[0], "%04d/%02d/%02d %02d:%02d:%02d ", impl->m_now.t->tm_year+1900, impl->m_now.t->tm_mon+1,
                        impl->m_now.t->tm_mday, impl->m_now.t->tm_hour, impl->m_now.t->tm_min, impl->m_now.t->tm_sec);

                if (impl->m_now.date != impl->m_last_date)      //日期更新，创建新的日志文件
                    impl->rotate_log();
            }

            if (impl->m_fp) {
                fwrite(impl->m_fmt_buf.c_str(), sizeof(char), impl->m_fmt_buf.size(), impl->m_fp);
                fwrite(cmd.logger_str.c_str(), sizeof(char), cmd.logger_str.size()-1, impl->m_fp);

                if (!strncmp(&cmd.logger_str[1], "FATAL", 5)) {
                    fatal_str = std::move(cmd.logger_str);
                    fflush(impl->m_fp);
                }
            }
        }

        if (!fatal_str.empty()) {
            fprintf(stderr, "%s", fatal_str.c_str());
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

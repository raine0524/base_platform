#pragma once

enum LOG_LEVEL
{
    LVL_DEBUG = 0,
    LVL_INFO,
    LVL_WARN,
    LVL_ERROR,
    LVL_FATAL,
};

#define log_debug(fmt, args...)     g_app_log.printf(LVL_DEBUG, fmt, ##args)

#define log_info(fmt, args...)      g_app_log.printf(LVL_INFO, fmt, ##args)

#define log_warn(fmt, args...)      g_app_log.printf(LVL_WARN, fmt, ##args)

#define log_error(fmt, args...)     g_app_log.printf(LVL_ERROR, fmt, ##args)

#define log_fatal(fmt, args...)     g_app_log.printf(LVL_FATAL, fmt, ##args)

namespace crx
{
    class CRX_SHARE logger : public kobj
    {
    public:
        void printf(LOG_LEVEL level, const char *fmt, ...);
    };
}

extern crx::logger g_app_log;

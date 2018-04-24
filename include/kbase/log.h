#pragma once

namespace crx
{
    class scheduler;
    class CRX_SHARE log : public kobj
    {
    public:
        void printf(const char *fmt, ...);

    protected:
        log() = default;
        friend scheduler;
    };

    extern log *g_log;

    #define log_error(fmt, args...) do { \
        crx::g_log->printf("[%s|%s|%d] [ERROR] " fmt, __FILENAME__, __func__, __LINE__, ##args); \
    } while(0)

    #define log_warn(fmt, args...) do { \
        crx::g_log->printf("[%s|%s|%d] [WARN] " fmt, __FILENAME__, __func__, __LINE__, ##args); \
    } while(0)

    #define log_info(fmt, args...) do { \
        crx::g_log->printf("[%s|%s|%d] [INFO] " fmt, __FILENAME__, __func__, __LINE__, ##args); \
    } while(0)

    #define log_debug(fmt, args...) do { \
        crx::g_log->printf("[%s|%s|%d] [DEBUG] " fmt, __FILENAME__, __func__, __LINE__, ##args); \
    } while(0)
}
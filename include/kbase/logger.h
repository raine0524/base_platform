#pragma once

#include "crx_pch.h"

#define log_error(log_ins, fmt, args...)    log_ins.printf("[ERROR] [%s|%s|%d] " fmt, __FILENAME__, __func__, __LINE__, ##args)

#define log_warn(log_ins, fmt, args...)     log_ins.printf("[WARN] [%s|%s|%d] "  fmt, __FILENAME__, __func__, __LINE__, ##args)

#define log_info(log_ins, fmt, args...)     log_ins.printf("[INFO] [%s|%s|%d] "  fmt, __FILENAME__, __func__, __LINE__, ##args)

#define log_debug(log_ins, fmt, args...)    log_ins.printf("[DEBUG] [%s|%s|%d] " fmt, __FILENAME__, __func__, __LINE__, ##args)

namespace crx
{
    class CRX_SHARE log : public kobj
    {
    public:
        void printf(const char *fmt, ...);

        //分离日志
        void detach();
    };
}
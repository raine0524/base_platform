#pragma once

#define CRX_EXPORT_SYMBOL

#include "../include/crx_pch.h"

enum CTL_CMD
{
    CMD_REG_NAME = 0,       //名字注册
    CMD_SVR_ONLINE,         //服务上线
    CMD_HELLO,              //握手
    CMD_GOODBYE,            //挥手
    CMD_CONN_CON,           //连接建立
    CMD_CONN_DES,           //连接断开
    CMD_MAX_VAL,
};

extern crx::logger g_lib_log;

namespace crx
{
    extern std::unordered_map<int, std::string> g_ext_type;

    extern std::map<std::string, std::string> g_ws_headers;

    extern std::unordered_map<int, std::string> g_sts_desc;
}

//////////////////////////////////////////////////////////////////////////
//kbase library (self custome implementation)
#include "net_base.h"
#include "scheduler_impl.h"
#include "schutil_impl.h"
#include "tcp_proto_impl.h"
#include "http_proto_impl.h"
#include "console_impl.h"

//mock system object
#include "sys_hook.h"
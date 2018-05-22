#pragma once

#define CRX_EXPORT_SYMBOL

#include "../include/crx_pch.h"

enum CTL_CMD {
    CMD_REG_NAME = 0,       //名字注册
    CMD_SVR_ONLINE,         //服务上线
    CMD_HELLO,              //握手
    CMD_GOODBYE,            //挥手
    CMD_CONN_CON,           //连接建立
    CMD_CONN_DES,           //连接断开
    CMD_MAX_VAL,
};

//////////////////////////////////////////////////////////////////////////
//kbase library (self custome implementation)
//msgpack
#include "../include/msgpack.hpp"

#include "net_base.h"
#include "pywrap_impl.h"
#include "sched_impl.h"
#include "console_impl.h"
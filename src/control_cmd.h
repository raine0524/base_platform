#pragma once

enum CTL_CMD {
    CMD_REG_NAME = 0,       //名字注册
    CMD_SVR_ONLINE,         //服务上线
    CMD_HELLO,              //握手
    CMD_GOODBYE,            //挥手
    CMD_CONN_CON,           //连接建立
    CMD_CONN_DES,           //连接断开
    CMD_MAX_VAL,
};

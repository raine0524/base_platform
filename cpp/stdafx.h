#pragma once

#define CRX_EXPORT_SYMBOL

#include "../include/crx_pch.h"

extern crx::logger g_lib_log;

//////////////////////////////////////////////////////////////////////////
//kbase library (self custome implementation)
#include "net_base.h"
#include "scheduler_impl.h"
#include "schutil_impl.h"
#include "tcp_proto_impl.h"
#include "http_proto_impl.h"
#include "console_impl.h"
#include "etcd_wrap_impl.h"

//mock system object
#include "sys_hook.h"

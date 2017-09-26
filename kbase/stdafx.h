#pragma once

#define CRX_EXPORT_SYMBOL

#include "../include/crx_pch.h"

//////////////////////////////////////////////////////////////////////////
//kbase library (self custome implementation)
//msgpack
#include "../include/msgpack.hpp"

#include "net_base.h"
#include "epoll_thread_impl.h"
#include "console_impl.h"
#include "evd_thread_impl.h"
#include "py_wrapper_impl.h"

namespace crx
{
    extern std::string g_server_name;
}
#pragma once

#define CRX_EXPORT_SYMBOL

#include "../include/crx_pch.h"

//////////////////////////////////////////////////////////////////////////
//kbase library (self custome implementation)
//msgpack
#include "../include/msgpack.hpp"

#include "net_base.h"
#include "console_impl.h"
#include "evdth_impl.h"
#include "pywrap_impl.h"
#include "sched_impl.h"

namespace crx
{
    extern std::string g_server_name;
}
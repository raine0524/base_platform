#include "stdafx.h"

namespace crx
{
    class log_impl
    {
    public:
        log_impl() {}
        virtual ~log_impl() {}
    };

    log::log()
    {
        m_obj = new log_impl;
    }

    log::~log()
    {
        delete (log_impl*)m_obj;
    }
}
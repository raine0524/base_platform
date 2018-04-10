#pragma once

#include "crx_pch.h"

namespace crx
{
    class CRX_SHARE ini : public kobj
    {
    public:
        ini();
        virtual ~ini();

        //加载ini文件
        bool load(const char *file_name);
    };
}

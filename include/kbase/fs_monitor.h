#pragma once

#include "crx_pch.h"

namespace crx
{
    class CRX_SHARE fs_monitor : public kobj
    {
    public:
        /*
         * 监控指定的文件系统对象，path可以是目录或是文件，也可以是已挂载的移动介质中的对象(可多次调用同时监控多个目标)
         * @param path 文件系统对象
         * @param mask 监控事件，默认的监控事件为创建/删除/修改
         * @param recursive 若path为目录，则该参数指明是否对该目录下的所有子目录进行递归监控
         */
        void add_watch(const char *path, uint32_t mask = IN_CREATE | IN_DELETE | IN_MODIFY, bool recursive = true);

        /*
         * 移除正在监控的对象
         * @param path 文件系统对象
         * @param recursive 若path为目录，该参数指明是否递归移除子目录的监控
         */
        void rm_watch(const char *path, bool recursive = true);
    };
}
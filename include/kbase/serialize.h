#pragma once

#include "crx_pch.h"

namespace crx
{
    class CRX_SHARE seria : public kobj
    {
    public:
        /*
         * use_simp标志指明这一组件是否用于网络传输中的序列化操作，且应用层使用simp协议
         * 若是那么seria组件将会预留额外的空间用于组装满足协议格式的封包，否则不做任何预留
         */
        seria(bool use_simp = false);
        virtual ~seria() {}

        //序列化操作，reset为重置操作，该seria对象可以重复使用
        void reset();

        //加入键值对，键始终为string类型
        void insert(const char *key, const char *data, size_t len);

        //所有键值对加入完毕之后，执行序列化操作 @comp指示是否需要压缩
        mem_ref get_string(bool comp = false);

        /*
         * 获取一次分片大小
         * @params为指定的内存块
         * @return value ret
         *          --> >0表示首地址为data 长度为ret的一段内存为一次完整分片
         *          --> =0需要更多的数据得到一次分片
         *          --> <0表示首地址为data 长度为abs(ret)的内存为非法数据，应丢弃
         */
        int get_sharding_size(const char *data, size_t len);

        //反序列化操作，将数据流反序列化为map表
        std::map<std::string, mem_ref> dump(const char *data, size_t len);
    };
}

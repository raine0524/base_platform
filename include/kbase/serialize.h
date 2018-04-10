#pragma once

namespace crx
{
    class CRX_SHARE seria : public kobj
    {
    public:
        seria();
        virtual ~seria();

        //序列化操作，reset为重置操作，该seria对象可以重复使用
        void reset();

        //加入键值对，键始终为string类型
        void insert(const char *key, const char *data, size_t len);

        //所有键值对加入完毕之后，执行序列化操作 @comp指示是否需要压缩
        mem_ref get_string(int comp = false);

        //反序列化操作，将数据流反序列化为map表
        std::unordered_map<const char*, mem_ref> dump(const char *data, int len);
    };
}

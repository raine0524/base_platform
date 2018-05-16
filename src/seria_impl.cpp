#include "stdafx.h"

namespace crx
{
    using msgpack::type::raw_ref;

    class seria_impl : public impl
    {
    public:
        seria_impl(bool use_simp) : m_packer(&m_pack_buf), m_use_simp(use_simp) {}

        void deseria(const char *data, size_t len);		//反序列化操作

        msgpack::sbuffer m_pack_buf;
        msgpack::packer<msgpack::sbuffer> m_packer;

        bool m_use_simp;
        simp_header m_stub_header;

        std::string m_origin_buf;
        std::unordered_map<std::string, mem_ref> m_dump_map;
    };

    seria::seria(bool use_simp /*= false*/)
    {
        auto impl = std::make_shared<seria_impl>(use_simp);
        if (use_simp)
            impl->m_packer.pack_str_body((const char*)&impl->m_stub_header, sizeof(simp_header));
        /*
         * 在序列化串中预留5个字节的空间，第一个字节为int32类型在msgpack中对应的标志，后面的4个字节
         * 用于指明当前整个序列化串的大小
         */
        int32_t pack_size = use_simp ? sizeof(simp_header)+5 : 5;
        impl->m_packer.pack_fix_int32(pack_size);
        m_impl = impl;
    }

    void seria::reset()
    {
        auto impl = std::dynamic_pointer_cast<seria_impl>(m_impl);
        //在执行重置操作时除将缓冲区pack_buf清空之外，同样需要预留5个字节的空间
        impl->m_pack_buf.clear();

        if (impl->m_use_simp)
            impl->m_packer.pack_str_body((const char*)&impl->m_stub_header, sizeof(simp_header));
        int32_t pack_size = impl->m_use_simp ? sizeof(simp_header)+5 : 5;
        impl->m_packer.pack_fix_int32(pack_size);
    }

    void seria::insert(const char *key, const char *data, size_t len)
    {
        auto impl = std::dynamic_pointer_cast<seria_impl>(m_impl);
        raw_ref rkey(key, strlen(key));
        raw_ref rval(data, len);
        std::pair<raw_ref, raw_ref> pair(rkey, rval);
        impl->m_packer.pack(pair);		//将键值对加入序列化串中

        int32_t *hk_sz = nullptr;
        if (impl->m_use_simp)
            hk_sz = (int32_t*)(impl->m_pack_buf.data()+sizeof(simp_header)+1);
        else
            hk_sz = (int32_t*)(impl->m_pack_buf.data()+1);
        *hk_sz = (int32_t)impl->m_pack_buf.size();		//更新当前字符串的大小
    }

    mem_ref seria::get_string(bool comp /*= false*/)
    {
        auto impl = std::dynamic_pointer_cast<seria_impl>(m_impl);
        if (comp) {     //使用ZIP算法对原始序列化串进行压缩
            size_t org_len = impl->m_pack_buf.size();
            size_t dst_len = compressBound(org_len);		//根据原始大小计算压缩之后整个字符串的大小

            std::string tmp_buf(dst_len, 0);
            compress((Bytef*)&tmp_buf[0], &dst_len, (const Bytef*)impl->m_pack_buf.data(), org_len);
            tmp_buf.resize(dst_len);

            //压缩完成之后构造一个二级的序列化串
            reset();
            insert("__comp__", (const char*)&comp, sizeof(comp));           //指明当前序列化串是否经过压缩
            insert("__org_len__", (const char*)&org_len, sizeof(org_len));  //原始字符串的长度
            insert("__data__", tmp_buf.data(), tmp_buf.size());             //指向压缩之后的字符串
        }

        // 构造序列化串，返回其内存引用
        return mem_ref(impl->m_pack_buf.data(), impl->m_pack_buf.size());
    }

    int seria::get_sharding_size(const char *data, size_t len)
    {
        if (len < 5)        //头5个字节用于确定整个序列化串的大小
            return 0;

        if ((char)0xd2 != *data) {		//验证第一个字节的魔数是否等于0xd2u
            size_t i = 1;
            for (; i < len; ++i)
                if ((char)0xd2 == data[i])
                    break;

            if (i < len)        //在后续流中找到该魔数，截断该魔数之前的无效流
                return -(int)i;
            else                //未找到
                return -(int)len;
        }

        int ctx_len = *(int*)(data+1);		//获取序列化串的大小
        if (len < ctx_len)
            return 0;
        else
            return ctx_len;
    }

    void seria_impl::deseria(const char *data, size_t len)
    {
        size_t off = 0;
        bool fetch_size = false;
        while (off != len) {
            msgpack::unpacked up;
            msgpack::unpack(up, data, len, off,
                            [](msgpack::type::object_type type, std::size_t size, void* user_data) { return true; });

            if (!fetch_size) {		//该string的第一个块存放的是整个字符串的长度，将其过滤
                fetch_size = true;
                continue;
            }

            //从序列化串中构造相应的键值对
            std::pair<raw_ref, raw_ref> pair = up.get().convert();
            m_dump_map[std::string(pair.first.ptr, pair.first.size)] = mem_ref(pair.second.ptr, pair.second.size);
        }
    }

    std::unordered_map<std::string, mem_ref> seria::dump(const char *data, size_t len)
    {
        auto impl = std::dynamic_pointer_cast<seria_impl>(m_impl);
        impl->m_dump_map.clear();
        impl->deseria(data, len);		//反序列化

        //不存在key为__comp__的键值对，不需要解压缩
        if (impl->m_dump_map.end() == impl->m_dump_map.find("__comp__"))
            return impl->m_dump_map;

        size_t dst_len = *(size_t*)impl->m_dump_map["__org_len__"].data;		//获取原始字符串的大小
        impl->m_origin_buf.resize(dst_len, 0);

        //解压缩操作，并再次进行反序列化
        auto ref = impl->m_dump_map["__data__"];
        uncompress((Bytef*)&impl->m_origin_buf[0], &dst_len, (const Bytef*)ref.data, ref.len);
        impl->m_dump_map.clear();
        impl->deseria(impl->m_origin_buf.data(), impl->m_origin_buf.size());
        return impl->m_dump_map;
    }
}

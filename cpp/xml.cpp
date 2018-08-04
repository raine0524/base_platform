#include "stdafx.h"

namespace crx
{
    class xml_impl : public impl
    {
    public:
        xml_impl() : m_cn(nullptr) {}
        virtual ~xml_impl() {}

        /*
         * 判断当前是否存在指定的孩子节点，存在则返回true，反之则为false
         * @node_name：孩子节点名
         * @switch_cn：是否发生切换操作
         */
        bool access_child(const char *node_name, bool switch_cn);

        std::string m_xml_file;		//xml文件名
        tinyxml2::XMLElement *m_cn;		//当前节点
        tinyxml2::XMLDocument m_doc;	//xml文档
    };

    bool xml_impl::access_child(const char *node_name, bool switch_cn)
    {
        tinyxml2::XMLElement *node = nullptr;
        if (m_cn)		//当前节点存在，说明已指向指定的节点
            node = m_cn->FirstChildElement(node_name);
        else			//否则使用doc来定位指定的孩子节点
            node = m_doc.FirstChildElement(node_name);

        bool find = false;
        if (node) {		//存在指定的孩子节点
            if (switch_cn)
                m_cn = node;		//执行切换操作
            find = true;
        }
        return find;
    }

    xml_parser::xml_parser()
    {
        m_impl = std::make_shared<xml_impl>();
    }

    xml_parser::~xml_parser()
    {
        flush();
    }

    bool xml_parser::load(const char *xml_file, const char *root /*= nullptr*/)
    {
        auto impl = std::dynamic_pointer_cast<xml_impl>(m_impl);
        if (tinyxml2::XML_NO_ERROR != impl->m_doc.LoadFile(xml_file))		//加载文件
            return false;

        impl->m_xml_file = xml_file;
        if (root)		//切换到指定的根节点
            impl->access_child(root, true);
        return true;
    }

    xml_parser& xml_parser::switch_parent()
    {
        auto impl = std::dynamic_pointer_cast<xml_impl>(m_impl);
        if (impl->m_cn)		//切换到父节点
            impl->m_cn = impl->m_cn->Parent()->ToElement();
        return *this;
    }

    xml_parser& xml_parser::switch_child(const char *node_name)
    {
        auto impl = std::dynamic_pointer_cast<xml_impl>(m_impl);    //切换到指定的孩子节点
        impl->access_child(node_name, true);
        return *this;
    }

    bool xml_parser::find_child(const char *node_name)
    {
        auto impl = std::dynamic_pointer_cast<xml_impl>(m_impl);    //查找指定的孩子节点
        return impl->access_child(node_name, false);
    }

    void xml_parser::set_child(const char *name, const char *value, bool flush /*= true*/)
    {
        if (!name)
            return;

        auto impl = std::dynamic_pointer_cast<xml_impl>(m_impl);
        if (impl->access_child(name, true)) {			//找到指定子节点，且发生切换操作
            set_value(value);		//设置节点的值
            switch_parent();
        } else {		//未找到，新增
            tinyxml2::XMLElement *child = impl->m_doc.NewElement(name);
            if (value)
                child->SetText(value);
            if (impl->m_cn)		//链接到当前节点之下或成为xml文件的根节点
                impl->m_cn->LinkEndChild(child);
            else
                impl->m_doc.LinkEndChild(child);
        }

        if (flush)		//实时更新xml文件
            impl->m_doc.SaveFile(impl->m_xml_file.c_str());
    }

    void xml_parser::delete_child(const char *name, bool flush /*= true*/)
    {
        if (!name)
            return;

        auto impl = std::dynamic_pointer_cast<xml_impl>(m_impl);
        tinyxml2::XMLElement *child = nullptr;
        while (true) {		//查找xml doc或当前节点下所有指定的孩子节点，并删除这些节点
            if (impl->m_cn) {
                child = impl->m_cn->FirstChildElement(name);
                if (child)
                    impl->m_cn->DeleteChild(child);
                else
                    break;
            } else {
                child = impl->m_doc.FirstChildElement(name);
                if (child)
                    impl->m_doc.DeleteChild(child);
                else
                    break;
            }
        }

        if (flush)		//实时更新xml文件
            impl->m_doc.SaveFile(impl->m_xml_file.c_str());
    }

    const char* xml_parser::value()
    {
        auto impl = std::dynamic_pointer_cast<xml_impl>(m_impl);
        if (!impl->m_cn)
            return nullptr;
        return impl->m_cn->GetText();		//获取当前节点的值
    }

    void xml_parser::set_value(const char *value, bool flush /*= true*/)
    {
        auto impl = std::dynamic_pointer_cast<xml_impl>(m_impl);
        if (!impl->m_cn)
            return;
        if (value)
            impl->m_cn->SetText(value);		//设置当前节点的值
        else
            impl->m_cn->DeleteChildren();
        if (flush)		//实时更新xml文件
            impl->m_doc.SaveFile(impl->m_xml_file.c_str());
    }

    const char* xml_parser::attribute(const char *attr_name)
    {
        auto impl = std::dynamic_pointer_cast<xml_impl>(m_impl);
        if (!impl->m_cn)
            return nullptr;
        return impl->m_cn->Attribute(attr_name);		//获取当前节点指定属性的值
    }

    void xml_parser::set_attribute(const char *name, const char *value, bool flush /*= true*/)
    {
        auto impl = std::dynamic_pointer_cast<xml_impl>(m_impl);
        if (!impl->m_cn)
            return;
        impl->m_cn->SetAttribute(name, value);		//设置当前节点指定属性的值
        if (flush)		//实时更新xml文件
            impl->m_doc.SaveFile(impl->m_xml_file.c_str());
    }

    void xml_parser::delete_attribute(const char *name, bool flush /*= true*/)
    {
        auto impl = std::dynamic_pointer_cast<xml_impl>(m_impl);
        if (!impl->m_cn)
            return;
        impl->m_cn->DeleteAttribute(name);		//删除当前节点的指定属性
        if (flush)		//实时更新xml文件
            impl->m_doc.SaveFile(impl->m_xml_file.c_str());
    }

    void xml_parser::flush()
    {
        auto impl = std::dynamic_pointer_cast<xml_impl>(m_impl);
        impl->m_doc.SaveFile(impl->m_xml_file.c_str());		//刷新当前缓存
    }

    void xml_parser::for_each_attr(std::function<void(std::string&, std::string&)> f)
    {
        auto impl = std::dynamic_pointer_cast<xml_impl>(m_impl);
        if (!impl->m_cn)
            return;

        const tinyxml2::XMLAttribute *attr = impl->m_cn->FirstAttribute();
        while (attr) {		//对当前节点的每一个属性依次执行相应的回调函数
            std::string name = attr->Name();
            std::string value = attr->Value();
            f(name, value);
            attr = attr->Next();
        }
    }

    void xml_parser::for_each_child(std::function<void(std::string&, std::string&, std::map<std::string, std::string>&)> f)
    {
        auto impl = std::dynamic_pointer_cast<xml_impl>(m_impl);
        if (!impl->m_cn)
            return;

        tinyxml2::XMLElement *child = impl->m_cn->FirstChildElement();
        while (child) {
            std::string name = child->Name();			//节点名称
            std::string value;		                    //节点值
            const char *text = child->GetText();
            if (text)
                value = text;
            std::map<std::string, std::string> attr_map;		//节点的键值对属性

            const tinyxml2::XMLAttribute *attr = child->FirstAttribute();
            while (attr) {			//获取当前节点的所有属性
                attr_map[attr->Name()] = attr->Value();
                attr = attr->Next();		//获取下一个属性
            }
            f(name, value, attr_map);               //执行回调函数
            child = child->NextSiblingElement();    //获取当前节点的下一个兄弟节点
        }
    }
}

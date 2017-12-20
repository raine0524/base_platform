#pragma once

namespace crx
{
	class CRX_SHARE xml_parser : public kobj
	{
	public:
		xml_parser();

		virtual ~xml_parser();

		/**
         * 加载xml文件
         * @xml_file：文件名
         * @root：加载之后切换到的根节点
         */
		bool load(const char *xml_file, const char *root = nullptr);

		//将当前节点切换到父节点
		xml_parser& switch_parent();

		//将当前节点切换到指定的子节点
		xml_parser& switch_child(const char *node_name);

		//查找指定的孩子节点(不发生切换操作)
		bool find_child(const char *node_name);

		//设置指定子节点的值，若不存在则创建
		void set_child(const char *name, const char *value, bool flush = true);

		//删除指定孩子节点
		void delete_child(const char *name, bool flush = true);

		//获取当前节点的值
		const char* value();

		//设置当前节点的值
		void set_value(const char *value, bool flush = true);

		//获取当前节点指定属性的值
		const char* attribute(const char *attr_name);

		//设置指定属性的值，若不存在则创建
		void set_attribute(const char *name, const char *value, bool flush = true);

		//删除指定属性
		void delete_attribute(const char *name, bool flush = true);

		//将当前xml文件对应的缓冲实时更新到xml文件中
		void flush();

		//对当前节点的每一个属性执行相应的操作，回调函数中的三个参数依次为属性名、属性值及回调参数
		void for_each_attr(std::function<void(std::string&, std::string&, void*)> f, void *arg = nullptr);

		//对当前节点的每一个孩子节点执行相应的操作，回调函数中的4个参数依次为节点名、节点值、节点所有键值对属性及回调参数
		void for_each_child(std::function<void(std::string&, std::string&, std::unordered_map<std::string, std::string>&, void*)> f, void *arg = nullptr);
	};
}

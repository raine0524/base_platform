#include "crx_pch.h"

class conf_ini : public crx::console
{
public:
    virtual bool init(int argc, char *argv[]);

    virtual void destroy(){}

private:
    crx::ini m_ini;
};

bool conf_ini::init(int argc, char *argv[])
{
    m_ini.load("ini/test.ini");
    m_ini.print();
    printf("\n");

    m_ini.set_section("SEC_A");
    std::string name_A = m_ini.get_str("name");
    std::string sex_A = m_ini.get_str("sex");
    int age_A = m_ini.get_int("age");
    printf("SEC_A conf: name = %s, sex = %s, age = %d\n", name_A.c_str(), sex_A.c_str(), age_A);

    m_ini.set_section("SEC_B");
    std::string name_B = m_ini.get_str("name");
    std::string sex_B = m_ini.get_str("sex");
    int age_B = m_ini.get_int("age");
    printf("SEC_B conf: name = %s, sex = %s, age = %d\n", name_B.c_str(), sex_B.c_str(), age_B);
    return true;
}

int main(int argc, char *argv[])
{
    conf_ini ini;
    ini.run(argc, argv);
}

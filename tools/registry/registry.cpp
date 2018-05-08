#include "registry.h"

bool registry::init(int argc, char **argv)
{
    return true;
}

void registry::destroy()
{

}

int main(int argc, char *argv[])
{
    registry reg;
    return reg.run(argc, argv);
}
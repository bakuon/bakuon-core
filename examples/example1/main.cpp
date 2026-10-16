#pragma once

#include <iostream>

#include <bakuon/core/bakuon.h>

int main(int argc, char *argv[])
{
    std::cout << "Example 1" << std::endl;
    std::cout << "Bakuon version: " << bakuon::versionString() << std::endl;

    return 0;
}

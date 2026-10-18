#pragma once

#include <iostream>

#include <bakuon/core/bakuon.h>
#include <bakuon/core/domain.h>

int main(int argc, char *argv[])
{
    std::cout << "Example 1" << std::endl;
    std::cout << "Bakuon core version: " << bakuon::core::versionString() << std::endl;

    bakuon::core::DomainId id(10);
    std::cout << id.value() << "  " << id.named();

    return 0;
}

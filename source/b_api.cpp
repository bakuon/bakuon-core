#include "b_host.h"

using namespace bakuon::core;

extern "C" {

BAKUON_CORE_API std::uint32_t bakuon_host_abi_version()
{
    return IHost::abi_current;
}

BAKUON_CORE_API IHost* bakuon_host_create()
{
    try {
        return new Host();
    } catch (...) {
        return nullptr;
    }
}

BAKUON_CORE_API void bakuon_host_destroy(IHost* host)
{
    delete static_cast<Host*>(host);
}

} // extern "C"

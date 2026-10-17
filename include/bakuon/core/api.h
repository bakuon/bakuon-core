#pragma once

/** @file api.h  Export macro for symbols a plugin exports to the host. */

// Symbols a plugin exports to the host.
#ifndef BAKUON_PLUGIN_API
#    if defined(_WIN32)
#        define BAKUON_PLUGIN_API __declspec(dllexport)
#    else
#        define BAKUON_PLUGIN_API __attribute__((visibility("default")))
#    endif
#endif

// Symbols the host library exports to applications.
// The host library's own build defines BAKUON_CORE_BUILD.
#ifndef BAKUON_CORE_API
#    if defined(_WIN32)
#        if defined(BAKUON_BAKUON_BUILD)
#            define BAKUON_CORE_API __declspec(dllexport)
#        else
#            define BAKUON_CORE_API __declspec(dllimport)
#        endif
#    else
#        define BAKUON_CORE_API __attribute__((visibility("default")))
#    endif
#endif

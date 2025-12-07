#pragma once

#ifdef _WIN32
    #ifdef FAMILYVAULT_EXPORTS
        #define FV_API __declspec(dllexport)
    #else
        #define FV_API __declspec(dllimport)
    #endif
#else
    #define FV_API __attribute__((visibility("default")))
#endif


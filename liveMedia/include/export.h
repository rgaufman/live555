#pragma once

#ifdef __cplusplus
extern "C" {
#endif // #ifdef __cplusplus


#if _WIN32
#  define LIVEMEDIA_EXPORT_API __declspec (dllexport)
#  define LIVEMEDIA_IMPORT_API __declspec (dllimport)
#else // #if _WIN32
#  define LIVEMEDIA_EXPORT_API __attribute__ ((visibility("default")))
#  define LIVEMEDIA_IMPORT_API __attribute__ ((visibility("default")))
#endif // #if _WIN32


#ifndef LIVEMEDIA_STATIC
#  ifdef LIVEMEDIA_EXPORTS
#     define LIVEMEDIA_API LIVEMEDIA_EXPORT_API
#  else
#     define LIVEMEDIA_API LIVEMEDIA_IMPORT_API
#  endif // #ifdef LIVEMEDIA_EXPORTS
#else // #ifndef LIVEMEDIA_STATIC
#  define LIVEMEDIA_API
#endif // #ifndef LIVEMEDIA_STATIC


#ifdef __cplusplus
} // extern "C" {
#endif // #ifdef __cplusplus

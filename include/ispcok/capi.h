#ifndef ISPCOK_CAPI_H
#define ISPCOK_CAPI_H

#ifdef _WIN32
#if defined(ISPCOK_CAPI_EXPORTS)
#define ISPCOK_API __declspec(dllexport)
#else
#define ISPCOK_API __declspec(dllimport)
#endif
#else
#define ISPCOK_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

ISPCOK_API const char* ispcok_version(void);
ISPCOK_API int ispcok_run_modules(const char* modules_csv, const char* scenario, const char* plugin_dir, char** out_json);
ISPCOK_API void ispcok_free_string(char* ptr);

#ifdef __cplusplus
}
#endif

#endif

#if !defined(__PLATFORM_H)
#define __PLATFORM_H

#ifdef _MSC_VER
#define F_OK 0
#endif

// platform specific function for snprintf
#if defined(_MSC_VER) && _MSC_VER < 1900

#define snprintf(buf,len, format,...) _snprintf_s(buf, len,len, format, __VA_ARGS__)

#endif

#endif /* __PLATFORM_H */

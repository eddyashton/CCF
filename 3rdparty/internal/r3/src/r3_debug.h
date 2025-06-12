#ifndef R3_DEBUG_H
#define R3_DEBUG_H

#define R3_DEBUG 1
#ifdef R3_DEBUG

#  define info(fmt, ...) \
    do \
    { \
      fprintf( \
        stderr, \
        "[info] %s:%d:%s(): " fmt, \
        __FILE__, \
        __LINE__, \
        __func__, \
        ##__VA_ARGS__); \
    } while (0)

#  define debug(fmt, ...) \
    do \
    { \
      fprintf( \
        stderr, \
        "[debug] %s:%d:%s(): " fmt, \
        __FILE__, \
        __LINE__, \
        __func__, \
        ##__VA_ARGS__); \
    } while (0)

#else
#  define info(...) ;
#  define debug(...) ;
#endif

#endif /* !DEBUG_H */

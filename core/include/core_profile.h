#ifndef CORE_PROFILE_H
#define CORE_PROFILE_H

/* Accept either CORE_PROFILE_* or the legacy PICO_PROFILE_* selectors. */
#if defined(PICO_PROFILE_FULL) && !defined(CORE_PROFILE_FULL)
#define CORE_PROFILE_FULL 1
#endif
#if defined(PICO_PROFILE_RUNTIME) && !defined(CORE_PROFILE_RUNTIME)
#define CORE_PROFILE_RUNTIME 1
#endif
#if defined(PICO_PROFILE_EMBEDDED) && !defined(CORE_PROFILE_EMBEDDED)
#define CORE_PROFILE_EMBEDDED 1
#endif

#if (defined(CORE_PROFILE_FULL) ? 1 : 0) + \
    (defined(CORE_PROFILE_RUNTIME) ? 1 : 0) + \
    (defined(CORE_PROFILE_EMBEDDED) ? 1 : 0) > 1
#error "select exactly one CORE_PROFILE_*"
#endif

#if !defined(CORE_PROFILE_FULL) && \
    !defined(CORE_PROFILE_RUNTIME) && \
    !defined(CORE_PROFILE_EMBEDDED)
#define CORE_PROFILE_FULL 1
#endif

#if defined(CORE_PROFILE_EMBEDDED)
#define CORE_NO_HEAP 1
#define CORE_NO_STDIO 1
#ifndef CORE_EMBEDDED_HEAP_BYTES
#define CORE_EMBEDDED_HEAP_BYTES (256u * 1024u)
#endif
#endif

/* Compatibility aliases for the current pico codebase. */
#ifndef PICO_PROFILE_FULL
#define PICO_PROFILE_FULL CORE_PROFILE_FULL
#endif
#ifndef PICO_PROFILE_RUNTIME
#define PICO_PROFILE_RUNTIME CORE_PROFILE_RUNTIME
#endif
#ifndef PICO_PROFILE_EMBEDDED
#define PICO_PROFILE_EMBEDDED CORE_PROFILE_EMBEDDED
#endif
#ifndef PICO_NO_HEAP
#define PICO_NO_HEAP CORE_NO_HEAP
#endif
#ifndef PICO_NO_STDIO
#define PICO_NO_STDIO CORE_NO_STDIO
#endif
#ifndef PICO_EMBEDDED_HEAP_BYTES
#define PICO_EMBEDDED_HEAP_BYTES CORE_EMBEDDED_HEAP_BYTES
#endif

#endif /* CORE_PROFILE_H */
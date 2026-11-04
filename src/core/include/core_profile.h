#ifndef CORE_PROFILE_H
#define CORE_PROFILE_H

/* Two build profiles of the core library:
 *   FULL     - hosted build, heap allocation and stdio available (default)
 *   EMBEDDED - freestanding build with a static arena heap
 * A former RUNTIME profile compiled identical code to FULL and was
 * removed; CORE_NO_STDIO is reserved for the EMBEDDED profile but not
 * consumed by any core source yet. */

#if (defined(CORE_PROFILE_FULL) ? 1 : 0) + \
    (defined(CORE_PROFILE_EMBEDDED) ? 1 : 0) > 1
#error "select exactly one CORE_PROFILE_*"
#endif

#if !defined(CORE_PROFILE_FULL) && !defined(CORE_PROFILE_EMBEDDED)
#define CORE_PROFILE_FULL 1
#endif

#if defined(CORE_PROFILE_EMBEDDED)
#define CORE_NO_HEAP 1
#define CORE_NO_STDIO 1
#ifndef CORE_EMBEDDED_HEAP_BYTES
#define CORE_EMBEDDED_HEAP_BYTES (256u * 1024u)
#endif
#endif

#endif /* CORE_PROFILE_H */

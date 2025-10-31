/**
 * Minimal runtime support for plugins
 *
 * Provides basic libc functions that the compiler may generate calls to.
 */

typedef unsigned int size_t;

// memcpy - copy memory
void* memcpy(void* dest, const void* src, size_t n) {
  unsigned char* d = (unsigned char*)dest;
  const unsigned char* s = (const unsigned char*)src;
  while (n--) {
    *d++ = *s++;
  }
  return dest;
}

// memset - set memory
void* memset(void* s, int c, size_t n) {
  unsigned char* p = (unsigned char*)s;
  while (n--) {
    *p++ = (unsigned char)c;
  }
  return s;
}

// memmove - copy memory (handles overlaps)
void* memmove(void* dest, const void* src, size_t n) {
  unsigned char* d = (unsigned char*)dest;
  const unsigned char* s = (const unsigned char*)src;

  if (d < s) {
    while (n--) {
      *d++ = *s++;
    }
  } else if (d > s) {
    d += n;
    s += n;
    while (n--) {
      *--d = *--s;
    }
  }
  return dest;
}

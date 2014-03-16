/* MIN/MAX from libass */

#ifndef LIBASS_UTILS_H
#define LIBASS_UTILS_H

#define FFMAX(a,b) ((a) > (b) ? (a) : (b))
#define FFMIN(a,b) ((a) > (b) ? (b) : (a))
#define FFMINMAX(c,a,b) FFMIN(FFMAX(c, a), b)

#endif                          /* LIBASS_UTILS_H */

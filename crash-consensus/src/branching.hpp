#pragma once

/** Define a proposition as likely true.
 * @param prop Proposition
 **/
#undef likely
#ifdef __GNUC__
#define likely(prop) __builtin_expect(!!(prop), 1)
#else
#define likely(prop) (prop)
#endif

/** Define a proposition as likely false.
 * @param prop Proposition
 **/
#undef unlikely
#ifdef __GNUC__
#define unlikely(prop) __builtin_expect(!!(prop), 0)
#else
#define unlikely(prop) (prop)
#endif
#ifndef GRAMAS_CORO_H
#define GRAMAS_CORO_H

#include <stdint.h>

typedef uintptr_t coro_state_t;

/* A significantly better performing version that utilizes computed goto. */
#if __GNUC__ && ! USE_SWITCH_BASED_CORO

#define CO_BEGIN(__state) if (__state != 0) goto *((void *)(__state));

#define __CR_LABEL(__line) __coro_label ## __line
#define CO_LABEL(__line) __CR_LABEL(__line)

#define CO_YIELD(__state, __ret)	\
	do {	\
		__state = (coro_state_t)&&CO_LABEL(__LINE__);	\
		return __ret;	\
		CO_LABEL(__LINE__):;	\
	} while (0)

#define CO_RETURN(__state, __ret)	\
	do {	\
		__state = (coro_state_t)&&CO_LABEL(__LINE__);	\
		CO_LABEL(__LINE__):;\
		return __ret;	\
	} while (0)

#define CO_END abort();

#else /* Default variety using a switch statement. Will work everywhere but is
		 not nearly as performant and prevents the use of switch statements
		 inside coroutines. */

#define CO_BEGIN(__state) switch (__state) { case 0:;

#define CO_YIELD(__state, __ret)	\
	do {	\
		__state = __LINE__;	\
		return __ret;	\
		case __LINE__:;	\
	} while (0)

#define CO_RETURN(__state, __ret)	\
	do {	\
		__state = __LINE__;	\
		case __LINE__:	\
			return __ret;	\
	} while (0)

#define CO_END default: abort(); }

#endif /* Default variety */

#endif // GRAMAS_CORO_H

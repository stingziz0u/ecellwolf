/*
 * Compatibilidad newlib/PS3: este toolchain espera __ctype_ptr__ (y/o
 * __locale_ctype_ptr()) provisto por libc, pero la libc.a realmente
 * compilada no los exporta -- un hueco real de esta build de newlib.
 * Reconstruye la tabla exacta de clasificacion de la implementacion de
 * referencia de newlib (ctype_.c), con los valores de bits confirmados
 * en el propio ctype.h de ESTE toolchain (_U=01, _L=02, _N=04, _S=010,
 * _P=020, _C=040, _X=0100, _B=0200), asi isalpha/isdigit/etc dan las
 * respuestas correctas, no solo "algo que enlace".
 *
 * 128-255 en cero (sin clasificacion extendida por locale) es lo que
 * corresponde al locale "C" de ASCII plano.
 */

#define _U      01
#define _L      02
#define _N      04
#define _S      010
#define _P      020
#define _C      040
#define _X      0100
#define _B      0200

static const char ctype_table[1 + 256] = {
    0, /* indice para EOF (-1) */

    /* 0-127 */
    _C, _C, _C, _C, _C, _C, _C, _C,
    _C, _C|_S, _C|_S, _C|_S, _C|_S, _C|_S, _C, _C,
    _C, _C, _C, _C, _C, _C, _C, _C,
    _C, _C, _C, _C, _C, _C, _C, _C,
    _S|_B, _P, _P, _P, _P, _P, _P, _P,
    _P, _P, _P, _P, _P, _P, _P, _P,
    _N, _N, _N, _N, _N, _N, _N, _N,
    _N, _N, _P, _P, _P, _P, _P, _P,
    _P, _U|_X, _U|_X, _U|_X, _U|_X, _U|_X, _U|_X, _U,
    _U, _U, _U, _U, _U, _U, _U, _U,
    _U, _U, _U, _U, _U, _U, _U, _U,
    _U, _U, _U, _P, _P, _P, _P, _P,
    _P, _L|_X, _L|_X, _L|_X, _L|_X, _L|_X, _L|_X, _L,
    _L, _L, _L, _L, _L, _L, _L, _L,
    _L, _L, _L, _L, _L, _L, _L, _L,
    _L, _L, _L, _P, _P, _P, _P, _C,

    /* 128-255 */
    0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,
};

const char *__ctype_ptr__ = ctype_table + 1;

const char *
__locale_ctype_ptr(void)
{
    return ctype_table + 1;
}

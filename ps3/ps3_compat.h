// Compatibilidad newlib/PS3 -- se inyecta automaticamente en TODOS los
// archivos que se compilen, via la bandera -include del compilador, en
// vez de editar cada archivo del proyecto uno por uno.
#ifndef PS3_COMPAT_H
#define PS3_COMPAT_H

#include <strings.h>
#include <limits.h>

// newlib of the standard toolchain has no PATH_MAX (filesys.cpp)
#ifndef PATH_MAX
#define PATH_MAX 1024
#endif

#ifndef stricmp
#define stricmp strcasecmp
#endif
#ifndef strnicmp
#define strnicmp strncasecmp
#endif

// Marca propia, para poder excluir bloques de codigo especificos de
// escritorio (como el selector de KDE/kdialog en wl_iwad_picker.cpp)
// que de otra forma caen sin querer en cualquier plataforma que no sea
// explicitamente Windows/Mac/Android.
#define I_AM_PS3 1

#endif

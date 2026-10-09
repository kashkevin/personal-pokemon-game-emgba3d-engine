/* MinGW has no <alloca.h> (alloca() lives in <malloc.h>, which pulls in
 * <stdlib.h> and clashes with the game's abs() macro). Provide the GCC builtin
 * directly. Only on the include path of the Windows native build (Makefile_pc). */
#ifndef PC_PORT_ALLOCA_H
#define PC_PORT_ALLOCA_H
#ifndef alloca
#define alloca(size) __builtin_alloca(size)
#endif
#endif

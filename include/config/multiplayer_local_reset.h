#ifndef GUARD_CONFIG_MULTIPLAYER_LOCAL_RESET_H
#define GUARD_CONFIG_MULTIPLAYER_LOCAL_RESET_H
// Development-only reachable encounter. Off in ordinary builds.
#ifndef MP_LOCAL_RESET_DEVELOPMENT
#define MP_LOCAL_RESET_DEVELOPMENT 0
#endif
// One production-script pilot, still off pending ordinary-route acceptance.
#ifndef MP_LOCAL_RESET_NEW_MAUVILLE_PILOT
#define MP_LOCAL_RESET_NEW_MAUVILLE_PILOT 0
#endif
// Audited authored sources. Off pending ordinary-route release acceptance.
#ifndef MP_LOCAL_RESET_STATIC
#define MP_LOCAL_RESET_STATIC 0
#endif
// A published admission must yield the authored bytecode before inspecting TRUE/FALSE.
#define MP_LOCAL_RESET_ADMISSION_PENDING 2
#define MP_LOCAL_RESET_ENABLED (MP_LOCAL_RESET_DEVELOPMENT || MP_LOCAL_RESET_STATIC || MP_LOCAL_RESET_NEW_MAUVILLE_PILOT || TESTING)
#endif

/* Minimal Steam shim (no real Steam SDK).
 *
 * Dead Cells' title screen fires async Steam requests (request_stats /
 * request_global_stats) and then pumps steam_run_callbacks() every frame waiting
 * for the result events. With a pure no-op stub those events never arrive, so a
 * "loading/connecting" modal stays up forever -> the screen is dimmed by the
 * modal's dark scrim and keyboard input is captured by the modal (the game looks
 * dark and unresponsive on the title screen, while the studio/loading screens are
 * fine).
 *
 * This shim mirrors HeapsIO/hlsteam's contract just enough to unblock the menu:
 * steam_init() stores the event closure, and steam_run_callbacks() delivers the
 * UserStatsReceived / GlobalStatsReceived events the menu is waiting for. The
 * other steam_* natives remain log-once no-op stubs (emitted by the transpiler).
 *
 * Event ids come from hlsteam's event_type enum (native/steamwrap.h):
 *   None=0 GamepadTextInputDismissed=1 UserStatsReceived=2 UserStatsStored=3
 *   UserAchievementStored=4 LeaderboardFound=5 ScoreUploaded=6 ScoreDownloaded=7
 *   GlobalStatsReceived=8
 * The closure is invoked exactly as hlsteam's SendEvent() does.
 */
#define HL_NAME(n) steam_##n
#include <hl.h>
#include <stdio.h>

#define EVT_UserStatsReceived   2
#define EVT_GlobalStatsReceived 8

static vclosure *g_onEvent = 0;       /* (type:Int, success:Bool, data:Bytes) -> Void */
static vclosure *g_onGlobalEvent = 0; /* (id:Int, data:Dynamic) -> Void (unused here) */
static int g_pendingStats = 0;
static int g_pendingGlobalStats = 0;

static void steam_send_event( int type, bool success, vbyte *data ) {
	if( !g_onEvent ) return;
	if( g_onEvent->hasValue )
		((void(*)(void*,int,bool,vbyte*))g_onEvent->fun)(g_onEvent->value, type, success, data);
	else
		((void(*)(int,bool,vbyte*))g_onEvent->fun)(type, success, data);
}

HL_PRIM bool HL_NAME(init)( vclosure *onEvent, vclosure *onGlobalEvent ) {
	g_onEvent = onEvent;            hl_add_root(&g_onEvent);
	g_onGlobalEvent = onGlobalEvent; hl_add_root(&g_onGlobalEvent);
	fprintf(stderr, "[steam] init (shim) -> true\n"); fflush(stderr);
	return true; /* pass the DRM/ownership gate so the game doesn't quit */
}
DEFINE_PRIM(_BOOL, init, _FUN(_VOID, _I32 _BOOL _BYTES) _FUN(_VOID, _I32 _DYN));

HL_PRIM bool HL_NAME(request_stats)() { g_pendingStats = 1; return true; }
DEFINE_PRIM(_BOOL, request_stats, _NO_ARG);

HL_PRIM bool HL_NAME(request_global_stats)() { g_pendingGlobalStats = 1; return true; }
DEFINE_PRIM(_BOOL, request_global_stats, _NO_ARG);

HL_PRIM void HL_NAME(run_callbacks)() {
	/* Deliver the results the menu is waiting on (success=true, no payload, exactly
	   as hlsteam does for these events). Fired once per request. */
	if( g_pendingStats )       { g_pendingStats = 0;       steam_send_event(EVT_UserStatsReceived,   true, NULL); }
	if( g_pendingGlobalStats ) { g_pendingGlobalStats = 0; steam_send_event(EVT_GlobalStatsReceived, true, NULL); }
}
DEFINE_PRIM(_VOID, run_callbacks, _NO_ARG);

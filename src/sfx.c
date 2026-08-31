#include "sfx.h"

#include <math.h>
#include <psp2/audioout.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/clib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define SFX_RATE 48000
#define SFX_LEN  1536 /* ~32 ms */

static int s_port = -1;
static int16_t s_click[SFX_LEN * 2];

static void build_click(void)
{
	int i;
	/* Clic court type UI Sony : tick aigu + decay rapide */
	for (i = 0; i < SFX_LEN; i++) {
		float t = (float)i / (float)SFX_RATE;
		float env = expf(-t * 55.0f);
		float s = sinf(2.0f * (float)M_PI * 2100.0f * t) * env;
		/* léger 2e harmonique pour coller au « ploc » système */
		s += 0.25f * sinf(2.0f * (float)M_PI * 4200.0f * t) * env;
		int16_t v = (int16_t)(s * 12000.0f);
		s_click[i * 2 + 0] = v;
		s_click[i * 2 + 1] = v;
	}
}

int sfx_init(void)
{
	if (s_port >= 0)
		return 0;
	build_click();
	s_port = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_MAIN, SFX_LEN,
	                             SFX_RATE, SCE_AUDIO_OUT_MODE_STEREO);
	if (s_port < 0) {
		s_port = -1;
		return -1;
	}
	return 0;
}

void sfx_term(void)
{
	if (s_port >= 0) {
		sceAudioOutReleasePort(s_port);
		s_port = -1;
	}
}

void sfx_click(void)
{
	static SceUInt64 last_us;
	SceUInt64 now;

	if (s_port < 0)
		sfx_init();
	if (s_port < 0)
		return;
	/* Anti-spam L/R mode DEV : évite de bloquer la boucle sur l'audio */
	now = sceKernelGetProcessTimeWide();
	if (last_us != 0 && (now - last_us) < 120000ULL)
		return;
	last_us = now;
	sceAudioOutOutput(s_port, s_click);
}

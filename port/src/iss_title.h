/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The game builds this port supports: inFAMOUS Second Son 1.07, US (CUSA00223) or EU (CUSA00004).
 * Both carry the same 1.07 executable (the unwrapped ELF hashes alike, docs/CUSA00004_CAMERA.md),
 * so every hook that checks the title also checks its own code bytes before patching. */
#ifndef ISS_TITLE_H
#define ISS_TITLE_H
#include <string.h>
static inline int iss_title_serial(const char *serial) {
    return serial && (!strcmp(serial,"CUSA00223") || !strcmp(serial,"CUSA00004"));
}
static inline int iss_title_107(const char *serial,const char *version) {
    return iss_title_serial(serial) && version && !strcmp(version,"01.07");
}
#endif

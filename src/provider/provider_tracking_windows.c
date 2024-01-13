/*
 *
 * Copyright (C) 2024 Intel Corporation
 *
 * Under the Apache License v2.0 with LLVM Exceptions. See LICENSE.TXT.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 */

#include "provider_tracking.h"

#include <stdlib.h>
#include <windows.h>

static umf_memory_tracker_handle_t TRACKER = NULL;

#if defined(UMF_SHARED_LIBRARY)
BOOL APIENTRY DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {
    if (fdwReason == DLL_PROCESS_DETACH) {
        umfMemoryTrackerDestroy(TRACKER);
    } else if (fdwReason == DLL_PROCESS_ATTACH) {
        TRACKER = umfMemoryTrackerCreate();
    }
    return TRUE;
}

void umfTrackingMemoryProviderInit(void) {
    // do nothing, additional initialization not needed
}
#else
INIT_ONCE init_once_flag = INIT_ONCE_STATIC_INIT;

static void providerFini(void) { umfMemoryTrackerDestroy(TRACKER); }

BOOL CALLBACK providerInit(PINIT_ONCE InitOnce, PVOID Parameter,
                           PVOID *lpContext) {
    TRACKER = umfMemoryTrackerCreate();
    atexit(providerFini);
    return TRACKER ? TRUE : FALSE;
}

void umfTrackingMemoryProviderInit(void) {
    InitOnceExecuteOnce(&init_once_flag, providerInit, NULL, NULL);
}
#endif

umf_memory_tracker_handle_t umfMemoryTrackerGet(void) {
    return (umf_memory_tracker_handle_t)TRACKER;
}

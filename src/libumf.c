/*
 *
 * Copyright (C) 2024 Intel Corporation
 *
 * Under the Apache License v2.0 with LLVM Exceptions. See LICENSE.TXT.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 */

#include <stddef.h>

#include "base_alloc_global.h"
#include "ipc_cache.h"
#include "memspace_internal.h"
#include "provider_tracking.h"
#include "topology.h"
#include "utils_log.h"

umf_memory_tracker_handle_t TRACKER = NULL;
ipc_handle_mmaped_cache_handle_t IPC_MMAPED_CACHE = NULL;

static unsigned long long umfRefCount = 0;

int umfInit(void) {
    if (util_fetch_and_add64(&umfRefCount, 1) == 0) {
        util_log_init();
        TRACKER = umfMemoryTrackerCreate();
        IPC_MMAPED_CACHE = umfIpcHandleMmapedCacheCreate();
    }

    return (TRACKER) ? 0 : -1;
}

void umfTearDown(void) {
    if (util_fetch_and_add64(&umfRefCount, -1) == 1) {
#ifndef _WIN32
        umfMemspaceHostAllDestroy();
        umfMemspaceHighestCapacityDestroy();
        umfMemspaceHighestBandwidthDestroy();
        umfMemspaceLowestLatencyDestroy();
        umfDestroyTopology();
#endif
        ipc_handle_mmaped_cache_handle_t ipc_mmaped_cache = IPC_MMAPED_CACHE;
        IPC_MMAPED_CACHE = NULL;
        umfIpcHandleMmapedCacheDestroy(ipc_mmaped_cache);
        // make sure TRACKER is not used after being destroyed
        umf_memory_tracker_handle_t t = TRACKER;
        TRACKER = NULL;
        umfMemoryTrackerDestroy(t);

        umf_ba_destroy_global();
    }
}

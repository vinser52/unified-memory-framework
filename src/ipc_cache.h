/*
 *
 * Copyright (C) 2024 Intel Corporation
 *
 * Under the Apache License v2.0 with LLVM Exceptions. See LICENSE.TXT.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 */

#ifndef UMF_IPC_CACHE_H
#define UMF_IPC_CACHE_H 1

#include "umf/memory_provider.h"

#include "base_alloc.h"
#include "uthash.h"
#include "utils_concurrency.h"

typedef struct ipc_mmaped_handle_key_t {
    void *remote_base_ptr;
    umf_memory_provider_handle_t local_provider;
    int remote_pid;
} ipc_mmaped_handle_key_t;

typedef struct ipc_handle_cache_entry_t {
    UT_hash_handle hh;
    struct ipc_handle_cache_entry_t *next, *prev;
    ipc_mmaped_handle_key_t key;
    uint64_t ref_count;
    uint64_t handle_id;
    void *mmaped_base_ptr;
    size_t mmaped_size;
    os_mutex_t mmap_lock;
} ipc_handle_cache_entry_t;

typedef struct ipc_handle_mmaped_cache_t {
    os_mutex_t cache_lock;
    umf_ba_pool_t *cache_allocator;
    size_t max_size;
    size_t cur_size;
    ipc_handle_cache_entry_t *hash_table;
    ipc_handle_cache_entry_t *frequency_list;
} ipc_handle_mmaped_cache_t;

typedef ipc_handle_mmaped_cache_t *ipc_handle_mmaped_cache_handle_t;

ipc_handle_mmaped_cache_handle_t umfIpcHandleMmapedCacheCreate(void);

void umfIpcHandleMmapedCacheDestroy(ipc_handle_mmaped_cache_handle_t cache);

umf_result_t umfIpcHandleMmapedCacheGet(ipc_handle_mmaped_cache_handle_t cache,
                                        const ipc_mmaped_handle_key_t *key,
                                        uint64_t handle_id,
                                        ipc_handle_cache_entry_t **retEntry);

#endif /* UMF_IPC_CACHE_H */

/*
 * Copyright (C) 2024 Intel Corporation
 *
 * Under the Apache License v2.0 with LLVM Exceptions. See LICENSE.TXT.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
*/

#include "provider_ipc_level_zero_getpidfd.h"

#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h> /* For SYS_pidfd_open, SYS_pidfd_getfd */
#include <unistd.h>

typedef struct ze_getpidfd_memory_provider_t {
    umf_memory_provider_handle_t hUpstreamProvider;
    bool own_upstream;
} ze_getpidfd_memory_provider_t;

static umf_result_t ze_getpidfd_memory_provider_initialize(void *params,
                                                           void **provider) {
    if (provider == NULL || params == NULL) {
        return UMF_RESULT_ERROR_INVALID_ARGUMENT;
    }

    ze_getpidfd_memory_provider_t *ze_getpidfd_provider =
        (ze_getpidfd_memory_provider_t *)malloc(
            sizeof(ze_getpidfd_memory_provider_t));
    if (ze_getpidfd_provider == NULL) {
        return UMF_RESULT_ERROR_OUT_OF_HOST_MEMORY;
    }

    umf_provider_level_zero_getpidfd_params_t *pub_params =
        (umf_provider_level_zero_getpidfd_params_t *)params;
    ze_getpidfd_provider->hUpstreamProvider = pub_params->hUpstreamProvider;
    ze_getpidfd_provider->own_upstream = pub_params->own_upstream;

    *provider = ze_getpidfd_provider;
    return UMF_RESULT_SUCCESS;
}

static void ze_getpidfd_memory_provider_finalize(void *provider) {
    ze_getpidfd_memory_provider_t *ze_getpidfd_provider =
        (ze_getpidfd_memory_provider_t *)provider;
    if (ze_getpidfd_provider->own_upstream) {
        umfMemoryProviderDestroy(ze_getpidfd_provider->hUpstreamProvider);
    }
    free(ze_getpidfd_provider);
}

static umf_result_t ze_getpidfd_memory_provider_alloc(void *provider,
                                                      size_t size,
                                                      size_t alignment,
                                                      void **resultPtr) {
    ze_getpidfd_memory_provider_t *ze_getpidfd_provider =
        (ze_getpidfd_memory_provider_t *)provider;
    return umfMemoryProviderAlloc(ze_getpidfd_provider->hUpstreamProvider, size,
                                  alignment, resultPtr);
}

static umf_result_t ze_getpidfd_memory_provider_free(void *provider, void *ptr,
                                                     size_t size) {
    ze_getpidfd_memory_provider_t *ze_getpidfd_provider =
        (ze_getpidfd_memory_provider_t *)provider;
    return umfMemoryProviderFree(ze_getpidfd_provider->hUpstreamProvider, ptr,
                                 size);
}

static void ze_getpidfd_memory_provider_get_last_native_error(
    void *provider, const char **ppMsg, int32_t *pError) {
    ze_getpidfd_memory_provider_t *ze_getpidfd_provider =
        (ze_getpidfd_memory_provider_t *)provider;
    umfMemoryProviderGetLastNativeError(ze_getpidfd_provider->hUpstreamProvider,
                                        ppMsg, pError);
}

static umf_result_t ze_getpidfd_memory_provider_get_recommended_page_size(
    void *provider, size_t size, size_t *pageSize) {
    ze_getpidfd_memory_provider_t *ze_getpidfd_provider =
        (ze_getpidfd_memory_provider_t *)provider;
    return umfMemoryProviderGetRecommendedPageSize(
        ze_getpidfd_provider->hUpstreamProvider, size, pageSize);
}

static umf_result_t
ze_getpidfd_memory_provider_get_min_page_size(void *provider, void *ptr,
                                              size_t *pageSize) {
    ze_getpidfd_memory_provider_t *ze_getpidfd_provider =
        (ze_getpidfd_memory_provider_t *)provider;
    return umfMemoryProviderGetMinPageSize(
        ze_getpidfd_provider->hUpstreamProvider, ptr, pageSize);
}

static const char *ze_getpidfd_memory_provider_get_name(void *provider) {
    (void)provider;
    return "ze_getpidfd_memory_provider";
}

static umf_result_t
ze_getpidfd_memory_provider_purge_lazy(void *provider, void *ptr, size_t size) {
    ze_getpidfd_memory_provider_t *ze_getpidfd_provider =
        (ze_getpidfd_memory_provider_t *)provider;
    return umfMemoryProviderPurgeLazy(ze_getpidfd_provider->hUpstreamProvider,
                                      ptr, size);
}

static umf_result_t ze_getpidfd_memory_provider_purge_force(void *provider,
                                                            void *ptr,
                                                            size_t size) {
    ze_getpidfd_memory_provider_t *ze_getpidfd_provider =
        (ze_getpidfd_memory_provider_t *)provider;
    return umfMemoryProviderPurgeForce(ze_getpidfd_provider->hUpstreamProvider,
                                       ptr, size);
}

static umf_result_t
ze_getpidfd_memory_provider_allocation_merge(void *provider, void *lowPtr,
                                             void *highPtr, size_t totalSize) {
    ze_getpidfd_memory_provider_t *ze_getpidfd_provider =
        (ze_getpidfd_memory_provider_t *)provider;
    return umfMemoryProviderAllocationMerge(
        ze_getpidfd_provider->hUpstreamProvider, lowPtr, highPtr, totalSize);
}

static umf_result_t ze_getpidfd_memory_provider_allocation_split(
    void *provider, void *ptr, size_t totalSize, size_t firstSize) {
    ze_getpidfd_memory_provider_t *ze_getpidfd_provider =
        (ze_getpidfd_memory_provider_t *)provider;
    return umfMemoryProviderAllocationSplit(
        ze_getpidfd_provider->hUpstreamProvider, ptr, totalSize, firstSize);
}

static umf_result_t
ze_getpidfd_memory_provider_get_ipc_handle_size(void *provider, size_t *size) {
    ze_getpidfd_memory_provider_t *ze_getpidfd_provider =
        (ze_getpidfd_memory_provider_t *)provider;
    return umfMemoryProviderGetIPCHandleSize(
        ze_getpidfd_provider->hUpstreamProvider, size);
}

static umf_result_t
ze_getpidfd_memory_provider_get_ipc_handle(void *provider, const void *ptr,
                                           size_t size, void *providerIpcData) {
    ze_getpidfd_memory_provider_t *ze_getpidfd_provider =
        (ze_getpidfd_memory_provider_t *)provider;
    return umfMemoryProviderGetIPCHandle(
        ze_getpidfd_provider->hUpstreamProvider, ptr, size, providerIpcData);
}

static umf_result_t
ze_getpidfd_memory_provider_put_ipc_handle(void *provider,
                                           void *providerIpcData) {
    ze_getpidfd_memory_provider_t *ze_getpidfd_provider =
        (ze_getpidfd_memory_provider_t *)provider;
    return umfMemoryProviderPutIPCHandle(
        ze_getpidfd_provider->hUpstreamProvider, providerIpcData);
}

int get_pid_from_ipc(void *providerIpcData) {
    // TODO: hacky way to get pid from providerIpcData
    typedef struct umf_ipc_data_t {
        int pid;         // process ID of the process that allocated the memory
        size_t baseSize; // size of base (coarse-grain) allocation
        uint64_t offset;
        char providerIpcData[];
    } umf_ipc_data_t;

    umf_ipc_data_t *ipcData =
        (umf_ipc_data_t *)((uint8_t *)providerIpcData - sizeof(umf_ipc_data_t));
    return ipcData->pid;
}

static umf_result_t
ze_getpidfd_memory_provider_open_ipc_handle(void *provider,
                                            void *providerIpcData, void **ptr) {
    ze_getpidfd_memory_provider_t *ze_getpidfd_provider =
        (ze_getpidfd_memory_provider_t *)provider;

    int pid = get_pid_from_ipc(providerIpcData);

    int pid_fd = syscall(SYS_pidfd_open, pid, 0);
    if (pid_fd == -1) {
        return UMF_RESULT_ERROR_UNKNOWN;
    }

    int remote_fd;
    // providerIpcData is just a pointer to the ze_ipc_handle.
    // ze_ipc_handle is opaque data structure but we uses such
    // way to get file descriptor from ze_ipc_handle
    memcpy(&remote_fd, providerIpcData, sizeof(int));
    int local_fd = syscall(SYS_pidfd_getfd, pid_fd, remote_fd, 0);
    close(pid_fd);
    if (local_fd == -1) {
        return UMF_RESULT_ERROR_UNKNOWN;
    }

    memcpy(providerIpcData, &local_fd, sizeof(int));

    return umfMemoryProviderOpenIPCHandle(
        ze_getpidfd_provider->hUpstreamProvider, providerIpcData, ptr);
}

static umf_result_t ze_getpidfd_memory_provider_close_ipc_handle(void *provider,
                                                                 void *ptr,
                                                                 size_t size) {
    ze_getpidfd_memory_provider_t *ze_getpidfd_provider =
        (ze_getpidfd_memory_provider_t *)provider;
    return umfMemoryProviderCloseIPCHandle(
        ze_getpidfd_provider->hUpstreamProvider, ptr, size);
}

static struct umf_memory_provider_ops_t
    UMF_LEVEL_ZERO_GETPIDFD_MEMORY_PROVIDER_OPS = {
        .version = UMF_VERSION_CURRENT,
        .initialize = ze_getpidfd_memory_provider_initialize,
        .finalize = ze_getpidfd_memory_provider_finalize,
        .alloc = ze_getpidfd_memory_provider_alloc,
        .free = ze_getpidfd_memory_provider_free,
        .get_last_native_error =
            ze_getpidfd_memory_provider_get_last_native_error,
        .get_recommended_page_size =
            ze_getpidfd_memory_provider_get_recommended_page_size,
        .get_min_page_size = ze_getpidfd_memory_provider_get_min_page_size,
        .get_name = ze_getpidfd_memory_provider_get_name,
        .ext.purge_lazy = ze_getpidfd_memory_provider_purge_lazy,
        .ext.purge_force = ze_getpidfd_memory_provider_purge_force,
        .ext.allocation_merge = ze_getpidfd_memory_provider_allocation_merge,
        .ext.allocation_split = ze_getpidfd_memory_provider_allocation_split,
        .ipc.get_ipc_handle_size =
            ze_getpidfd_memory_provider_get_ipc_handle_size,
        .ipc.get_ipc_handle = ze_getpidfd_memory_provider_get_ipc_handle,
        .ipc.put_ipc_handle = ze_getpidfd_memory_provider_put_ipc_handle,
        .ipc.open_ipc_handle = ze_getpidfd_memory_provider_open_ipc_handle,
        .ipc.close_ipc_handle = ze_getpidfd_memory_provider_close_ipc_handle,
};

umf_memory_provider_ops_t *umfLevelZeroGetPidFdMemoryProviderOps(void) {
    return &UMF_LEVEL_ZERO_GETPIDFD_MEMORY_PROVIDER_OPS;
}
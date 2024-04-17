/*
 *
 * Copyright (C) 2024 Intel Corporation
 *
 * Under the Apache License v2.0 with LLVM Exceptions. See LICENSE.TXT.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 */

#include <stdio.h>

#include "umf/ipc.h"
#include "umf/memory_pool.h"
#include "umf/pools/pool_disjoint.h"
#include "umf/providers/provider_level_zero.h"

#include "utils_level_zero.h"

int create_level_zero_pool(ze_context_handle_t context,
                           ze_device_handle_t device,
                           umf_memory_pool_handle_t *pool) {
    // setup params
    level_zero_memory_provider_params_t params = {0};
    params.level_zero_context_handle = context;
    params.level_zero_device_handle = device;
    params.memory_type = UMF_MEMORY_TYPE_DEVICE;
    // create Level Zero provider
    umf_memory_provider_handle_t provider = 0;
    umf_result_t umf_result = umfMemoryProviderCreate(
        umfLevelZeroMemoryProviderOps(), &params, &provider);
    if (umf_result != UMF_RESULT_SUCCESS) {
        fprintf(stderr, "Failed to create Level Zero memory provider!\n");
        return -1;
    }

    // create pool
    umf_pool_create_flags_t flags = UMF_POOL_CREATE_FLAG_OWN_PROVIDER;
    umf_disjoint_pool_params_t disjoint_params = umfDisjointPoolParamsDefault();
    umf_result = umfPoolCreate(umfDisjointPoolOps(), provider, &disjoint_params,
                               flags, pool);
    if (umf_result != UMF_RESULT_SUCCESS) {
        fprintf(stderr, "Failed to create pool!\n");
        return -1;
    }

    return 0;
}

int main(void) {
    uint32_t driver_idx = 0;
    ze_driver_handle_t driver = NULL;
    ze_device_handle_t device = NULL;
    ze_context_handle_t producer_context = NULL;
    ze_context_handle_t consumer_context = NULL;
    int ret = init_level_zero();
    if (ret != 0) {
        fprintf(stderr, "Failed to init Level 0!\n");
        return ret;
    }

    ret = find_driver_with_gpu(&driver_idx, &driver);
    if (ret || driver == NULL) {
        fprintf(stderr, "Cannot find L0 driver with GPU device!\n");
        return ret;
    }

    ret = create_context(driver, &producer_context);
    if (ret != 0) {
        fprintf(stderr, "Failed to create L0 context!\n");
        return ret;
    }

    ret = create_context(driver, &consumer_context);
    if (ret != 0) {
        fprintf(stderr, "Failed to create L0 context!\n");
        return ret;
    }

    ret = find_gpu_device(driver, &device);
    if (ret || device == NULL) {
        fprintf(stderr, "Cannot find GPU device!\n");
        return ret;
    }

    // create producer pool
    umf_memory_pool_handle_t producer_pool = 0;
    ret = create_level_zero_pool(producer_context, device, &producer_pool);
    if (ret != 0) {
        fprintf(stderr, "Failed to create producer pool!\n");
        return ret;
    }

    fprintf(stdout, "Producer pool created.\n");

    void *initial_buf = umfPoolMalloc(producer_pool, 1024);
    if (!initial_buf) {
        fprintf(stderr, "Failed to allocate buffer from UMF pool!\n");
        return -1;
    }

    fprintf(stdout, "Buffer allocated from the producer pool.\n");

    umf_ipc_handle_t ipc_handle = NULL;
    size_t handle_size = 0;
    umf_result_t umf_result =
        umfGetIPCHandle(initial_buf, &ipc_handle, &handle_size);
    if (umf_result != UMF_RESULT_SUCCESS) {
        fprintf(stderr, "Failed to get IPC handle!\n");
        return -1;
    }

    fprintf(stdout, "IPC handle obtained.\n");

    // create consumer pool
    umf_memory_pool_handle_t consumer_pool = 0;
    ret = create_level_zero_pool(consumer_context, device, &consumer_pool);
    if (ret != 0) {
        fprintf(stderr, "Failed to create consumer pool!\n");
        return ret;
    }

    fprintf(stdout, "Consumer pool created.\n");

    void *mapped_buf = NULL;
    umf_result = umfOpenIPCHandle(consumer_pool, ipc_handle, &mapped_buf);
    if (umf_result != UMF_RESULT_SUCCESS) {
        fprintf(stderr, "Failed to open IPC handle!\n");
        return -1;
    }

    fprintf(stdout, "IPC handle opened in the consumer pool.\n");

    umf_result = umfPutIPCHandle(ipc_handle);
    if (umf_result != UMF_RESULT_SUCCESS) {
        fprintf(stderr, "Failed to put IPC handle!\n");
        return -1;
    }

    fprintf(stdout, "IPC handle released in the producer pool.\n");

    umf_result = umfCloseIPCHandle(mapped_buf);
    if (umf_result != UMF_RESULT_SUCCESS) {
        fprintf(stderr, "Failed to close IPC handle!\n");
        return -1;
    }

    fprintf(stdout, "IPC handle closed in the consumer pool.\n");

    umfFree(initial_buf);

    umfPoolDestroy(producer_pool);
    umfPoolDestroy(consumer_pool);

    ret = destroy_context(producer_context);
    if (ret != 0) {
        fprintf(stderr, "Failed to destroy L0 context!\n");
        return ret;
    }

    ret = destroy_context(consumer_context);
    if (ret != 0) {
        fprintf(stderr, "Failed to destroy L0 context!\n");
        return ret;
    }
    return 0;
}

/*
 *   Copyright (c) 2023 Intel Corporation
 *   Licensed under the Apache License, Version 2.0 (the "License");
 *   you may not use this file except in compliance with the License.
 *   You may obtain a copy of the License at
 *       http://www.apache.org/licenses/LICENSE-2.0
 *   Unless required by applicable law or agreed to in writing, software
 *   distributed under the License is distributed on an "AS IS" BASIS,
 *   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
 *   or implied.
 *   See the License for the specific language governing
 *   permissions and
 *   limitations under the License.
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include <level_zero/ze_api.h>

#include "umf/disjoint_pool/disjoint_pool.h"
#include "umf/memory_providers/ze_memory_provider.h"
#include "umf/memory_providers/fixed_memory_provider.h"

ze_context_handle_t context;
ze_device_handle_t device;

void create_context()
{

    zeInit(ZE_INIT_FLAG_GPU_ONLY);

    uint32_t driverCount = 1;
    ze_driver_handle_t driverHandle;
    zeDriverGet(&driverCount, &driverHandle);

    uint32_t deviceCount = 1;
    zeDeviceGet(driverHandle, &deviceCount, &device);
    assert(device);

    ze_context_desc_t context_desc = {};
    context_desc.stype = ZE_STRUCTURE_TYPE_CONTEXT_DESC;
    zeContextCreate(driverHandle, &context_desc, &context);
    assert(context);
}

int main()
{
    create_context();

    ze_device_mem_alloc_desc_t ze_device_mem_alloc_desc = {};
    ze_device_mem_alloc_desc.stype = ZE_STRUCTURE_TYPE_DEVICE_MEM_ALLOC_DESC;
    ze_memory_provider_params_t ze_memory_provider_params = {
        .context = context,
        .device = device,
        .device_mem_alloc_desc = ze_device_mem_alloc_desc};

    umf_memory_provider_handle_t ze_memory_provider;
    umfMemoryProviderCreate(&UMF_ZE_MEMORY_PROVIDER_OPS,
                            &ze_memory_provider_params,
                            &ze_memory_provider);
    assert(ze_memory_provider);

    const size_t KB = 1024;
    const size_t MB = 1024 * KB;

    fixed_memory_provider_params_t fixed_memory_provider_params = {
        .init_buffer = 0,
        .init_buffer_size = 0,
        .upstream_memory_provider = ze_memory_provider,
        .immediate_init = false,
        .soft_limit = 384 * MB,
        .hard_limit = 1024 * MB};

    umf_memory_provider_handle_t fixed_memory_provider;
    umfMemoryProviderCreate(&UMF_FIXED_MEMORY_PROVIDER_OPS,
                            &fixed_memory_provider_params,
                            &fixed_memory_provider);
    assert(fixed_memory_provider);

    disjoint_memory_pool_params_t disjoint_memory_pool_params = {/* TODO */};

    umf_memory_pool_handle_t ze_disjoint_memory_pool;
    umfPoolCreate(&DISJOINT_POOL_OPS, &fixed_memory_provider, 1, NULL,
                  &ze_disjoint_memory_pool);
    assert(ze_disjoint_memory_pool);

    // test 1

    size_t s1 = 74659 * KB;
    size_t s2 = 8206 * KB;

    // s1
    for (int j = 0; j < 2; j++)
    {
        void *t[6] = {0};
        for (int i = 0; i < 6; i++)
        {
            t[i] = umfPoolMalloc(ze_disjoint_memory_pool, s1);
            assert(t[i]);
        }

        for (int i = 0; i < 6; i++)
        {
            umf_result_t res = umfPoolFree(ze_disjoint_memory_pool, t[i]);
            assert(res == UMF_RESULT_SUCCESS);
        }
    }

    // s2
    for (int j = 0; j < 2; j++)
    {
        void *t[6] = {0};
        for (int i = 0; i < 6; i++)
        {
            t[i] = umfPoolMalloc(ze_disjoint_memory_pool, s2);
            assert(t[i]);
        }

        for (int i = 0; i < 6; i++)
        {
            umf_result_t res = umfPoolFree(ze_disjoint_memory_pool, t[i]);
            assert(res == UMF_RESULT_SUCCESS);
        }
    }

    printf("SUCCESS\n");

    return 0;
}

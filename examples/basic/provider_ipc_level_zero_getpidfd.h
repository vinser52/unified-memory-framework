// Copyright (C) 2024 Intel Corporation
// Under the Apache License v2.0 with LLVM Exceptions. See LICENSE.TXT.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef UMF_LEVEL_ZERO_GETPIDFD_PROVIDER_H
#define UMF_LEVEL_ZERO_GETPIDFD_PROVIDER_H

#include <stdbool.h>

#include <umf/memory_provider.h>

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct umf_provider_level_zero_getpidfd_params_t {
    umf_memory_provider_handle_t hUpstreamProvider;
    bool own_upstream;
} umf_provider_level_zero_getpidfd_params_t;

umf_memory_provider_ops_t *umfLevelZeroGetPidFdMemoryProviderOps(void);

#if defined(__cplusplus)
}
#endif

#endif // UMF_LEVEL_ZERO_GETPIDFD_PROVIDER_H
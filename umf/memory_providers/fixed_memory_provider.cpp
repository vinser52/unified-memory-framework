/*
    Copyright (c) 2023 Intel Corporation
    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at
        http://www.apache.org/licenses/LICENSE-2.0
    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.
*/

#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <stdbool.h>

#include "critnib/critnib.h"

#include "fixed_memory_provider.h"

typedef struct block_t
{
    size_t size;
    size_t used_size;
    void *data;

    bool upstream;
    struct block_t *next;
} block_t;

typedef struct fixed_memory_provider_t
{
    void *init_buffer;
    size_t init_buffer_size;

    umf_memory_provider_handle_t upstream_memory_provider;

    size_t current_size;
    size_t soft_limit;
    size_t hard_limit;

    pthread_mutex_t lock;
    block_t *free_list_head;
} fixed_memory_provider_t;

enum umf_result_t fixed_memory_provider_initialize(void *params, void **provider)
{
    fixed_memory_provider_params_t *fixed_params =
        (fixed_memory_provider_params_t *)params;

    // TODO check limits
    assert(fixed_params->soft_limit <= fixed_params->hard_limit);

    fixed_memory_provider_t *fixed_provider =
        (fixed_memory_provider_t *)malloc(sizeof(fixed_memory_provider_t));
    if (!fixed_provider)
    {
        return UMF_RESULT_ERROR_OUT_OF_HOST_MEMORY;
    }

    if (pthread_mutex_init(&fixed_provider->lock, NULL) != 0)
    {
        return UMF_RESULT_ERROR_MEMORY_PROVIDER_SPECIFIC;
    }

    fixed_provider->init_buffer = fixed_params->init_buffer;
    fixed_provider->init_buffer_size = fixed_params->init_buffer_size;
    fixed_provider->upstream_memory_provider = fixed_params->upstream_memory_provider;

    fixed_provider->soft_limit = fixed_params->soft_limit;
    fixed_provider->hard_limit = fixed_params->hard_limit;

    if (fixed_provider->init_buffer)
    {
        fixed_provider->free_list_head->upstream = false;
        fixed_provider->current_size = fixed_params->init_buffer_size;
    }
    else if (fixed_provider->upstream_memory_provider &&
             fixed_provider->soft_limit &&
             fixed_params->immediate_init)
    {
        printf("FIXED_ALLOC (init_upstream) %lu\n", fixed_provider->soft_limit);

        umfMemoryProviderAlloc(fixed_provider->upstream_memory_provider,
                               fixed_provider->soft_limit, 0,
                               &fixed_provider->free_list_head->data);
        // TODO error handling
        assert(fixed_provider->free_list_head->data);
        fixed_provider->free_list_head->upstream = true;
        fixed_provider->current_size = fixed_params->soft_limit;
    }
    else
    {
        fixed_provider->current_size = 0;
    }

    if (fixed_provider->current_size > 0)
    {
        fixed_provider->free_list_head = (block_t *)malloc(sizeof(block_t));
        fixed_provider->free_list_head->next = NULL;
        fixed_provider->free_list_head->used_size = 0;
        fixed_provider->free_list_head->size = fixed_provider->current_size;
    }
    else
    {
        fixed_provider->free_list_head = NULL;
    }

    *provider = fixed_provider;

    // TODO check results
    return UMF_RESULT_SUCCESS;
}

void fixed_memory_provider_finalize(void *provider)
{
    fixed_memory_provider_t *fixed_provider = (struct fixed_memory_provider_t *)provider;

    block_t *block = fixed_provider->free_list_head;
    while (block)
    {
        if (block->upstream)
        {
            umfMemoryProviderFree(fixed_provider->upstream_memory_provider,
                                  block->data, block->size);
        }

        block_t *t = block;
        block = block->next;
        free(t);
    }

    pthread_mutex_destroy(&fixed_provider->lock);
    free(fixed_provider->free_list_head);
    free(fixed_provider);
}

static enum umf_result_t fixed_memory_provider_alloc(void *provider, size_t size,
                                                     size_t alignment,
                                                     void **resultPtr)
{
    fixed_memory_provider_t *fixed_provider = (struct fixed_memory_provider_t *)provider;
    assert(fixed_provider);

    if (pthread_mutex_lock(&fixed_provider->lock) != 0)
        return UMF_RESULT_ERROR_MEMORY_PROVIDER_SPECIFIC;

    if ((fixed_provider->hard_limit) > 0 &&
        (fixed_provider->current_size + size > fixed_provider->hard_limit))
    {
        printf("FIXED_ALLOC ERROR OOM\n");
        // TODO - out of provider memory?
        return UMF_RESULT_ERROR_MEMORY_PROVIDER_SPECIFIC;
    }

    block_t *prev = NULL;
    block_t *curr = fixed_provider->free_list_head;
    while (curr)
    {
        assert(curr->size >= curr->used_size);
        size_t size_left = curr->size - curr->used_size;
        if (size_left > size)
        {
            // split
            block_t *new_block = (block_t *)malloc(sizeof(block_t));
            new_block->size = size;
            new_block->used_size = size;
            new_block->upstream = curr->upstream;
            new_block->next = curr;
            new_block->data = (unsigned char *)curr->data + curr->used_size;

            curr->size -= size;

            if (prev)
            {
                prev->next = new_block;
            }
            else
            {
                fixed_provider->free_list_head = new_block;
            }

            *resultPtr = new_block->data;
            fixed_provider->current_size += size;

            printf("FIXED_ALLOC (new_block) %lu curr %lu\n",
                   size,
                   fixed_provider->current_size);

            if (pthread_mutex_unlock(&fixed_provider->lock) != 0)
                return UMF_RESULT_ERROR_MEMORY_PROVIDER_SPECIFIC;

            return UMF_RESULT_SUCCESS;
        }
        else if (size_left == size)
        {
            printf("FIXED_ALLOC (same_block) %lu curr %lu\n",
                   size,
                   fixed_provider->current_size);

            *resultPtr = curr->data;
            curr->used_size = size;
            fixed_provider->current_size += size;

            if (pthread_mutex_unlock(&fixed_provider->lock) != 0)
                return UMF_RESULT_ERROR_MEMORY_PROVIDER_SPECIFIC;

            return UMF_RESULT_SUCCESS;
        }

        prev = curr;
        curr = curr->next;
    }

    // no suitable block - try to get more memory from the upstream provider
    if (fixed_provider->upstream_memory_provider)
    {
        umfMemoryProviderAlloc(fixed_provider->upstream_memory_provider, size, alignment, resultPtr);

        if (*resultPtr == NULL)
            return UMF_RESULT_ERROR_MEMORY_PROVIDER_SPECIFIC;

        block_t *new_block = (block_t *)malloc(sizeof(block_t));
        new_block->size = size;
        new_block->used_size = size;
        new_block->upstream = true;
        new_block->next = fixed_provider->free_list_head;
        new_block->data = *resultPtr;

        fixed_provider->free_list_head = new_block;
        fixed_provider->current_size += size;

        printf("FIXED_ALLOC (upstream) %lu curr %lu\n", size, fixed_provider->current_size);

        if (pthread_mutex_unlock(&fixed_provider->lock) != 0)
            return UMF_RESULT_ERROR_MEMORY_PROVIDER_SPECIFIC;

        return UMF_RESULT_SUCCESS;
    }

    return UMF_RESULT_ERROR_OUT_OF_HOST_MEMORY;
}

static enum umf_result_t fixed_memory_provider_free(void *provider, void *ptr,
                                                    size_t bytes)
{
    fixed_memory_provider_t *fixed_provider = (struct fixed_memory_provider_t *)provider;
    assert(fixed_provider);

    block_t *block = fixed_provider->free_list_head;
    block_t *prev = NULL;
    while (block && block->data != ptr)
    {
        prev = block;
        block = block->next;
    }

    if (block == NULL)
        return UMF_RESULT_ERROR_MEMORY_PROVIDER_SPECIFIC;

    if ((fixed_provider->current_size > fixed_provider->soft_limit) &&
        block->upstream)
    {
        printf("FIXED_FREE (soft_limit_%lu) %lu curr %lu\n",
               fixed_provider->soft_limit,
               block->size,
               fixed_provider->current_size);

        umfMemoryProviderFree(fixed_provider->upstream_memory_provider, block->data, block->size);

        if (prev)
        {
            prev->next = block->next;
        }
        else
        {
            fixed_provider->free_list_head = block->next;
        }

        fixed_provider->current_size -= block->size;
        free(block);
        return UMF_RESULT_SUCCESS;
    }
    // else
    // TODO implement blocks coalescing
    printf("FIXED_FREE (return_block_to_pool) %lu curr %lu\n",
           block->size,
           fixed_provider->current_size - block->size);

    block->used_size = 0;
    fixed_provider->current_size -= block->size;

    return UMF_RESULT_SUCCESS;
}

void fixed_memory_provider_get_last_native_error(void *provider,
                                                 const char **ppMessage,
                                                 int32_t *pError)
{
    //
}

static enum umf_result_t
fixed_memory_provider_get_min_page_size(void *provider, void *ptr,
                                        size_t *pageSize)
{
    *pageSize = 0; // TODO
    return UMF_RESULT_SUCCESS;
}

const char *fixed_memory_provider_get_name(void *provider) { return "UMF_FIXED"; }

struct umf_memory_provider_ops_t UMF_FIXED_MEMORY_PROVIDER_OPS = {
    .version = UMF_VERSION_CURRENT,
    .initialize = fixed_memory_provider_initialize,
    .finalize = fixed_memory_provider_finalize,
    .alloc = fixed_memory_provider_alloc,
    .free = fixed_memory_provider_free,
    .get_last_native_error = fixed_memory_provider_get_last_native_error,
    .get_min_page_size = fixed_memory_provider_get_min_page_size,
    .get_name = fixed_memory_provider_get_name,
};

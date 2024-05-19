/*
 * Copyright (C) 2024 Intel Corporation
 *
 * Under the Apache License v2.0 with LLVM Exceptions. See LICENSE.TXT.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <umf/ipc.h>
#include <umf/memory_pool.h>
#include <umf/pools/pool_disjoint.h>
#include <umf/providers/provider_level_zero.h>

#include "utils_level_zero.h"

#define INET_ADDR "127.0.0.1"
#define MSG_SIZE 1024
#define SIZE_SHM 1024

int producer_connect_to_consumer(int port) {
    struct sockaddr_in consumer_addr;
    int producer_socket = -1;

    // create a producer socket
    producer_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (producer_socket < 0) {
        fprintf(stderr, "[producer] ERROR: Unable to create socket\n");
        return -1;
    }

    fprintf(stderr, "[producer] Socket created\n");

    // set IP address and port the same as for the consumer
    consumer_addr.sin_family = AF_INET;
    consumer_addr.sin_port = htons(port);
    consumer_addr.sin_addr.s_addr = inet_addr(INET_ADDR);

    // send connection request to the consumer
    if (connect(producer_socket, (struct sockaddr *)&consumer_addr,
                sizeof(consumer_addr)) < 0) {
        fprintf(stderr,
                "[producer] ERROR: unable to connect to the consumer\n");
        goto err_close_producer_socket_connect;
    }

    fprintf(stderr, "[producer] Connected to the consumer\n");

    return producer_socket; // success

err_close_producer_socket_connect:
    close(producer_socket);

    return -1;
}

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
        fprintf(stderr,
                "[producer] Failed to create Level Zero memory provider!\n");
        return -1;
    }

    // create pool
    umf_pool_create_flags_t flags = UMF_POOL_CREATE_FLAG_OWN_PROVIDER;
    umf_disjoint_pool_params_t disjoint_params = umfDisjointPoolParamsDefault();
    umf_result = umfPoolCreate(umfDisjointPoolOps(), provider, &disjoint_params,
                               flags, pool);
    if (umf_result != UMF_RESULT_SUCCESS) {
        fprintf(stderr, "[producer] Failed to create pool!\n");
        umfMemoryProviderDestroy(provider);
        return -1;
    }

    return 0;
}

int main(int argc, char *argv[]) {
    char recv_buffer[MSG_SIZE];
    int producer_socket;
    int ret = -1;
    uint32_t driver_idx = 0;
    ze_driver_handle_t driver = NULL;
    ze_device_handle_t device = NULL;
    ze_context_handle_t L0_context = NULL;

    if (argc < 2) {
        fprintf(stderr, "Usage: %s port\n", argv[0]);
        return -1;
    }

    int port = atoi(argv[1]);

    ret = init_level_zero();
    if (ret != 0) {
        fprintf(stderr, "Failed to init Level 0!\n");
        goto exit;
    }

    ret = find_driver_with_gpu(&driver_idx, &driver);
    if (ret || driver == NULL) {
        fprintf(stderr, "Cannot find L0 driver with GPU device!\n");
        goto exit;
    }

    ret = create_context(driver, &L0_context);
    if (ret != 0) {
        fprintf(stderr, "Failed to create L0 context!\n");
        goto exit;
    }

    ret = find_gpu_device(driver, &device);
    if (ret || device == NULL) {
        fprintf(stderr, "Cannot find GPU device!\n");
        goto exit;
    }

    umf_memory_pool_handle_t hPool = NULL;
    ret = create_level_zero_pool(L0_context, device, &hPool);
    if (ret != 0) {
        fprintf(stderr, "[producer] ERROR: Failed to create producer pool!\n");
        goto err_destroy_L0_context;
    }

    void *IPC_shared_memory;
    size_t size_IPC_shared_memory = SIZE_SHM;
    IPC_shared_memory = umfPoolMalloc(hPool, size_IPC_shared_memory);
    if (IPC_shared_memory == NULL) {
        fprintf(stderr, "[producer] ERROR: allocating memory failed\n");
        ret = -1;
        goto err_destroy_pool;
    }

    // save a random number (&hPool) in the shared memory
    unsigned long long SHM_number_1 = (unsigned long long)&hPool;
    ret = level_zero_copy(L0_context, device, IPC_shared_memory, &SHM_number_1,
                          sizeof(SHM_number_1));
    if (ret != 0) {
        fprintf(stderr,
                "[producer] ERROR: writing to the Level Zero memory failed\n");
        goto err_free_IPC_shared_memory;
    }

    fprintf(stderr, "[producer] My shared memory contains a number: %llu\n",
            SHM_number_1);

    // get the IPC handle
    size_t IPC_handle_size;
    umf_ipc_handle_t IPC_handle;
    umf_result_t umf_result =
        umfGetIPCHandle(IPC_shared_memory, &IPC_handle, &IPC_handle_size);
    if (umf_result != UMF_RESULT_SUCCESS) {
        fprintf(stderr, "[producer] ERROR: getting the IPC handle failed\n");
        goto err_free_IPC_shared_memory;
    }

    fprintf(stderr, "[producer] Got the IPC handle\n");

    // connect to the consumer
    producer_socket = producer_connect_to_consumer(port);
    if (producer_socket < 0) {
        goto err_PutIPCHandle;
    }

    // send a size of the IPC_handle to the consumer
    ssize_t len =
        send(producer_socket, &IPC_handle_size, sizeof(IPC_handle_size), 0);
    if (len < 0) {
        fprintf(stderr, "[producer] ERROR: unable to send the message\n");
        goto err_close_producer_socket;
    }

    fprintf(stderr,
            "[producer] Sent the size of the IPC handle (%zu) to the consumer "
            "(sent %zu bytes)\n",
            IPC_handle_size, len);

    // zero the recv_buffer buffer
    memset(recv_buffer, 0, sizeof(recv_buffer));

    // receive the consumer's confirmation
    len = recv(producer_socket, recv_buffer, sizeof(recv_buffer), 0);
    if (len < 0) {
        fprintf(stderr, "[producer] ERROR: error while receiving the "
                        "confirmation from the consumer\n");
        goto err_close_producer_socket;
    }

    size_t conf_IPC_handle_size = *(size_t *)recv_buffer;
    if (conf_IPC_handle_size == IPC_handle_size) {
        fprintf(stderr,
                "[producer] Received the correct confirmation (%zu) from the "
                "consumer (%zu bytes)\n",
                conf_IPC_handle_size, len);
    } else {
        fprintf(stderr,
                "[producer] Received an INCORRECT confirmation (%zu) from the "
                "consumer (%zu bytes)\n",
                conf_IPC_handle_size, len);
        goto err_close_producer_socket;
    }

    // send the IPC_handle of IPC_handle_size to the consumer
    len = send(producer_socket, IPC_handle, IPC_handle_size, 0);
    if (len < 0) {
        fprintf(stderr, "[producer] ERROR: unable to send the message\n");
        goto err_close_producer_socket;
    }

    fprintf(stderr,
            "[producer] Sent the IPC handle to the consumer (%zu bytes)\n",
            IPC_handle_size);

    // zero the recv_buffer buffer
    memset(recv_buffer, 0, sizeof(recv_buffer));

    // receive the consumer's response
    len = recv(producer_socket, recv_buffer, sizeof(recv_buffer) - 1, 0);
    if (len < 0) {
        fprintf(
            stderr,
            "[producer] ERROR: error while receiving the consumer's message\n");
        goto err_close_producer_socket;
    }

    fprintf(stderr, "[producer] Received the consumer's response: \"%s\"\n",
            recv_buffer);

    if (strncmp(recv_buffer, "SKIP", 5 /* length of "SKIP" + 1 */) == 0) {
        fprintf(stderr, "[producer] Received the \"SKIP\" response from the "
                        "consumer, skipping ...\n");
        ret = 1;
        goto err_close_producer_socket;
    }

    // read a new value from the shared memory
    unsigned long long SHM_number_2 = 0;
    ret = level_zero_copy(L0_context, device, &SHM_number_2, IPC_shared_memory,
                          sizeof(SHM_number_2));
    if (ret != 0) {
        fprintf(
            stderr,
            "[producer] ERROR: reading from the Level Zero memory failed\n");
        goto err_close_producer_socket;
    }

    // the expected correct value is: SHM_number_2 == (SHM_number_1 / 2)
    if (SHM_number_2 == (SHM_number_1 / 2)) {
        ret = 0; // got the correct value - success!
        fprintf(
            stderr,
            "[producer] The consumer wrote the correct value (the old one / 2) "
            "to my shared memory: %llu\n",
            SHM_number_2);
    } else {
        fprintf(
            stderr,
            "[producer] ERROR: The consumer did NOT write the correct value "
            "(the old one / 2 = %llu) to my shared memory: %llu\n",
            (SHM_number_1 / 2), SHM_number_2);
    }

err_close_producer_socket:
    close(producer_socket);

err_PutIPCHandle:
    umf_result = umfPutIPCHandle(IPC_handle);
    if (umf_result != UMF_RESULT_SUCCESS) {
        fprintf(stderr, "[producer] ERROR: putting the IPC handle failed\n");
    }

    fprintf(stderr, "[producer] Put the IPC handle\n");

err_free_IPC_shared_memory:
    (void)umfPoolFree(hPool, IPC_shared_memory);

err_destroy_pool:
    umfPoolDestroy(hPool);

err_destroy_L0_context:
    destroy_context(L0_context);

exit:

    if (ret == 0) {
        fprintf(stderr, "[producer] Shutting down (status OK) ...\n");
    } else if (ret == 1) {
        fprintf(stderr, "[producer] Shutting down (status SKIP) ...\n");
        ret = 0;
    } else {
        fprintf(stderr, "[producer] Shutting down (status ERROR) ...\n");
    }

    return ret;
}

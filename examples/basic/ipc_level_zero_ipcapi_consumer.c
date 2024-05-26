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

#include "provider_ipc_level_zero_getpidfd.h"
#include "utils_level_zero.h"

#define INET_ADDR "127.0.0.1"
#define SEND_BUFF_SIZE 256
#define RECV_BUFF_SIZE 32
#define NUM_BUFFERS 128ull

// consumer's response message
#define CONSUMER_MSG                                                           \
    "This is the consumer. I just wrote a new number directly into your "      \
    "shared memory!"

int consumer_connect_to_producer(int port) {
    struct sockaddr_in consumer_addr;
    struct sockaddr_in producer_addr;
    int producer_addr_len;
    int producer_socket = -1;
    int consumer_socket = -1;
    int ret = -1;

    // create a socket
    consumer_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (consumer_socket < 0) {
        fprintf(stderr, "[consumer] ERROR: creating socket failed\n");
        return -1;
    }

    fprintf(stderr, "[consumer] Socket created\n");

    // set the IP address and the port
    consumer_addr.sin_family = AF_INET;
    consumer_addr.sin_port = htons(port);
    consumer_addr.sin_addr.s_addr = inet_addr(INET_ADDR);

    // bind to the IP address and the port
    if (bind(consumer_socket, (struct sockaddr *)&consumer_addr,
             sizeof(consumer_addr)) < 0) {
        fprintf(stderr, "[consumer] ERROR: cannot bind to the port\n");
        goto err_close_consumer_socket;
    }

    fprintf(stderr, "[consumer] Binding done\n");

    // listen for the producer
    if (listen(consumer_socket, 1) < 0) {
        fprintf(stderr, "[consumer] ERROR: listen() failed\n");
        goto err_close_consumer_socket;
    }

    fprintf(stderr, "[consumer] Listening for incoming connections ...\n");

    // accept an incoming connection
    producer_addr_len = sizeof(producer_addr);
    producer_socket = accept(consumer_socket, (struct sockaddr *)&producer_addr,
                             (socklen_t *)&producer_addr_len);
    if (producer_socket < 0) {
        fprintf(stderr, "[consumer] ERROR: accept() failed\n");
        goto err_close_consumer_socket;
    }

    fprintf(stderr, "[consumer] Producer connected at IP %s and port %i\n",
            inet_ntoa(producer_addr.sin_addr), ntohs(producer_addr.sin_port));

    ret = producer_socket; // success

err_close_consumer_socket:
    close(consumer_socket);

    return ret;
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
    umf_memory_provider_handle_t l0Provider = 0;
    umf_result_t umf_result = umfMemoryProviderCreate(
        umfLevelZeroMemoryProviderOps(), &params, &l0Provider);
    if (umf_result != UMF_RESULT_SUCCESS) {
        fprintf(stderr,
                "[consumer] Failed to create Level Zero memory provider!\n");
        return -1;
    }

    umf_memory_provider_handle_t l0GetPidFdProvider = 0;
    umf_provider_level_zero_getpidfd_params_t getpidfd_params = {0};
    getpidfd_params.hUpstreamProvider = l0Provider;
    getpidfd_params.own_upstream = true;
    umf_result =
        umfMemoryProviderCreate(umfLevelZeroGetPidFdMemoryProviderOps(),
                                &getpidfd_params, &l0GetPidFdProvider);
    if (umf_result != UMF_RESULT_SUCCESS) {
        fprintf(stderr, "[consumer] Failed to create Level Zero GetPidFd "
                        "memory provider!\n");
        umfMemoryProviderDestroy(l0Provider);
        return -1;
    }

    // create pool
    umf_pool_create_flags_t flags = UMF_POOL_CREATE_FLAG_OWN_PROVIDER;
    umf_disjoint_pool_params_t disjoint_params = umfDisjointPoolParamsDefault();
    umf_result = umfPoolCreate(umfDisjointPoolOps(), l0GetPidFdProvider,
                               &disjoint_params, flags, pool);
    if (umf_result != UMF_RESULT_SUCCESS) {
        fprintf(stderr, "[consumer] Failed to create pool!\n");
        umfMemoryProviderDestroy(l0GetPidFdProvider);
        return -1;
    }

    return 0;
}

int main(int argc, char *argv[]) {
    char send_buffer[SEND_BUFF_SIZE];
    char recv_buffer[RECV_BUFF_SIZE];
    int producer_socket;
    int ret = -1;
    uint32_t driver_idx = 0;
    ze_driver_handle_t driver = NULL;
    ze_device_handle_t device = NULL;
    ze_context_handle_t L0_context = NULL;

    if (argc < 2) {
        fprintf(stderr, "usage: %s port\n", argv[0]);
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

    ret = find_gpu_device(driver, &device);
    if (ret || device == NULL) {
        fprintf(stderr, "Cannot find GPU device!\n");
        goto exit;
    }

    ret = create_context(driver, &L0_context);
    if (ret != 0) {
        fprintf(stderr, "Failed to create L0 context!\n");
        goto exit;
    }

    umf_memory_pool_handle_t hPool = NULL;
    ret = create_level_zero_pool(L0_context, device, &hPool);
    if (ret != 0) {
        fprintf(stderr, "[consumer] Failed to create pool!\n");
        goto err_destroy_L0_context;
    }

    // connect to the producer
    producer_socket = consumer_connect_to_producer(port);
    if (producer_socket < 0) {
        goto err_destroy_pool;
    }

    void *SHM_ptr[NUM_BUFFERS];
    for (size_t i = 0; i < NUM_BUFFERS; ++i) {
        memset(recv_buffer, 0, RECV_BUFF_SIZE);

        // receive a size of the IPC handle from the producer's
        ssize_t len = recv(producer_socket, recv_buffer, RECV_BUFF_SIZE, 0);
        if (len < 0) {
            fprintf(stderr, "[consumer] ERROR: receiving a size of the IPC "
                            "handle failed\n");
            goto err_close_producer_socket;
        }

        size_t size_IPC_handle = *(size_t *)recv_buffer;

        fprintf(
            stderr,
            "[consumer] Received %zu bytes - the size of the IPC handle: %zu "
            "bytes\n",
            len, size_IPC_handle);

        // send received size to the producer as a confirmation
        len =
            send(producer_socket, &size_IPC_handle, sizeof(size_IPC_handle), 0);
        if (len < 0) {
            fprintf(stderr, "[consumer] ERROR: sending confirmation failed\n");
            goto err_close_producer_socket;
        }

        fprintf(stderr,
                "[consumer] Sent a confirmation to the producer (%zu bytes)\n",
                len);

        // allocate memory for IPC handle
        umf_ipc_handle_t IPC_handle =
            (umf_ipc_handle_t)calloc(1, size_IPC_handle);
        if (IPC_handle == NULL) {
            fprintf(stderr,
                    "[consumer] ERROR: receiving the IPC handle failed\n");
            goto err_close_producer_socket;
        }

        // receive the IPC handle from the producer's
        len = recv(producer_socket, IPC_handle, size_IPC_handle, 0);
        if (len < 0) {
            fprintf(stderr,
                    "[consumer] ERROR: receiving the IPC handle failed\n");
            free(IPC_handle);
            continue;
        }
        if (len < size_IPC_handle) {
            fprintf(
                stderr,
                "[consumer] ERROR: receiving the IPC handle failed - received "
                "only %zu bytes (size of IPC handle is %zu bytes)\n",
                len, size_IPC_handle);
            free(IPC_handle);
            continue;
        }

        fprintf(stderr,
                "[consumer] Received the IPC handle from the producer (%zi "
                "bytes)\n",
                len);

        umf_result_t umf_result =
            umfOpenIPCHandle(hPool, IPC_handle, &(SHM_ptr[i]));
        if (umf_result == UMF_RESULT_ERROR_NOT_SUPPORTED) {
            fprintf(
                stderr,
                "[consumer] SKIP: opening the IPC handle is not supported\n");
            ret = 1; // SKIP

            // write the SKIP response to the send_buffer buffer
            strcpy(send_buffer, "SKIP");

            // send the SKIP response to the producer
            send(producer_socket, send_buffer, strlen(send_buffer) + 1, 0);

            free(IPC_handle);
            continue;
        }
        if (umf_result != UMF_RESULT_SUCCESS) {
            fprintf(stderr,
                    "[consumer] ERROR: opening the IPC handle failed\n");
            free(IPC_handle);
            continue;
        }

        fprintf(
            stderr,
            "[consumer] Opened the IPC handle received from the producer\n");

        // read the current value from the shared memory
        unsigned long long SHM_number_1 = 0;
        ret = level_zero_copy(L0_context, device, &SHM_number_1, SHM_ptr[i],
                              sizeof(SHM_number_1));
        if (ret != 0) {
            fprintf(stderr, "[consumer] ERROR: reading from the Level Zero "
                            "memory failed\n");
            goto err_CloseIPCHandle;
        }

        fprintf(stderr,
                "[consumer] Read the number from the producer's shared memory: "
                "%llu\n",
                SHM_number_1);

        // calculate the new value
        unsigned long long SHM_number_2 = SHM_number_1 / 2;

        // write the new number directly to the producer's shared memory
        ret = level_zero_copy(L0_context, device, SHM_ptr[i], &SHM_number_2,
                              sizeof(SHM_number_2));
        fprintf(
            stderr,
            "[consumer] Wrote a new number directly to the producer's shared "
            "memory: %llu\n",
            SHM_number_2);

        // write the response to the send_buffer buffer
        memset(send_buffer, 0, sizeof(send_buffer));
        strcpy(send_buffer, CONSUMER_MSG);

        // send response to the producer
        if (send(producer_socket, send_buffer, strlen(send_buffer) + 1, 0) <
            0) {
            fprintf(stderr, "[consumer] ERROR: send() failed\n");
            goto err_CloseIPCHandle;
        }

        fprintf(stderr, "[consumer] Sent a response message to the producer\n");

        ret = 0; // SUCCESS
    }

err_CloseIPCHandle:
    for (size_t i = 0; i < NUM_BUFFERS; ++i) {
        umf_result_t umf_result = umfCloseIPCHandle(SHM_ptr[i]);
        if (umf_result != UMF_RESULT_SUCCESS) {
            fprintf(stderr,
                    "[consumer] ERROR: closing the IPC handle failed\n");
        }
    }

    fprintf(stderr,
            "[consumer] Closed the IPC handle received from the producer\n");

err_close_producer_socket:
    close(producer_socket);

err_destroy_pool:
    umfPoolDestroy(hPool);

err_destroy_L0_context:
    destroy_context(L0_context);

exit:

    if (ret == 0) {
        fprintf(stderr, "[consumer] Shutting down (status OK) ...\n");
    } else if (ret == 1) {
        fprintf(stderr, "[consumer] Shutting down (status SKIP) ...\n");
        ret = 0;
    } else {
        fprintf(stderr, "[consumer] Shutting down (status ERROR) ...\n");
    }

    return ret;
}

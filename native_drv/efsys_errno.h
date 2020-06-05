/*
 * Copyright (c) 2017-2020 Xilinx, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#ifndef EFSYS_ERRNO_H
#define EFSYS_ERRNO_H
#include <base/vmkapi_status.h>

#define ENOTSUP     VMK_NOT_SUPPORTED
#define EINVAL      VMK_BAD_PARAM
#define EEXIST      VMK_EXISTS
#define ENOMEM      VMK_NO_MEMORY
#define EIO         VMK_IO_ERROR
#define ETIMEDOUT   VMK_TIMEOUT
#define EFAULT      VMK_INVALID_ADDRESS
#define E2BIG       VMK_EOVERFLOW
#define EBUSY       VMK_BUSY
#define EAGAIN      VMK_RETRY
#define ENOSPC      VMK_NO_SPACE
#define ENOENT      VMK_NOT_FOUND
#define EINTR       VMK_WAIT_INTERRUPTED
#define EFBIG       VMK_LIMIT_EXCEEDED
#define ENODEV      VMK_INVALID_TARGET
#define ENOTACTIVE  VMK_NOT_READY
#define EACCES      VMK_NO_ACCESS
#define EDEADLK     VMK_RANK_VIOLATION
#define EMSGSIZE    VMK_MESSAGE_TOO_LONG
#define EALREADY    VMK_EALREADY
#define ERANGE      VMK_RESULT_TOO_LARGE
#define EPROTO      VMK_EPROTONOSUPPORT

#endif /* EFSYS_ERRNO_H */

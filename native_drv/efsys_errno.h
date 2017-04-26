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
#define EINTR       VMK_ABORTED /* TBD : Check if this is the correct mapping */
#define EFBIG       VMK_LIMIT_EXCEEDED
#define ENODEV      VMK_INVALID_TARGET
#define ENOTACTIVE  VMK_NOT_READY /* TBD : Check if this is the correct mapping */
#define EACCES      VMK_NO_ACCESS
#define EDEADLK     VMK_RANK_VIOLATION
#define EMSGSIZE    VMK_MESSAGE_TOO_LONG
#define EALREADY    VMK_EALREADY
#define ERANGE      VMK_RESULT_TOO_LARGE

#endif /* EFSYS_ERRNO_H */

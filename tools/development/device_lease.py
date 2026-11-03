"""Nonblocking device ownership shared by a coordinator and its child tools."""
import fcntl
import os
from pathlib import Path

DEVICE_LOCK = Path('/tmp/y2-platform-v1-device.lock')


def open_lease(inherited_fd=None, path=DEVICE_LOCK):
    if inherited_fd is None:
        lease = path.open('a')
    else:
        lease = os.fdopen(os.dup(inherited_fd), 'r')
        actual, expected = os.fstat(lease.fileno()), path.stat()
        if (actual.st_dev, actual.st_ino) != (expected.st_dev, expected.st_ino):
            lease.close()
            raise ValueError('inherited lease names a different lock inode')
    try:
        fcntl.flock(lease, fcntl.LOCK_EX | fcntl.LOCK_NB)
    except BaseException:
        lease.close()
        raise
    return lease

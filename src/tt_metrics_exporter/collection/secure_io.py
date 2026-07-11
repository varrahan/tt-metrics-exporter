"""Descriptor-relative reads for untrusted node-state directories."""

from __future__ import annotations

import errno
import os
from pathlib import Path
import stat

from ..models import CollectionIssue, SourceDiagnostics


def valid_component(value: str, maximum_bytes: int = 128) -> bool:
    try:
        encoded = value.encode("ascii")
    except UnicodeEncodeError:
        return False
    return bool(value) and len(encoded) <= maximum_bytes and value not in {".", ".."} and "/" not in value and "\0" not in value and all(0x21 <= byte <= 0x7E for byte in encoded)


def _record_os_error(error: OSError, diagnostics: SourceDiagnostics) -> None:
    issue = CollectionIssue.PERMISSION_DENIED if error.errno in {errno.EACCES, errno.EPERM} else CollectionIssue.READ_FAILED
    diagnostics.issues[issue] += 1


class SecureDirectory:
    """An owned directory descriptor used for race-resistant relative access."""

    def __init__(self, descriptor: int) -> None:
        self._descriptor = descriptor

    @classmethod
    def open_root(cls, path: Path, diagnostics: SourceDiagnostics) -> SecureDirectory | None:
        try:
            descriptor = os.open(
                path,
                os.O_RDONLY | os.O_CLOEXEC | os.O_DIRECTORY | os.O_NOFOLLOW,
            )
        except OSError as error:
            issue = CollectionIssue.MISSING_ROOT if error.errno in {errno.ENOENT, errno.ENOTDIR} else CollectionIssue.PERMISSION_DENIED if error.errno in {errno.EACCES, errno.EPERM} else CollectionIssue.READ_FAILED
            diagnostics.issues[issue] += 1
            diagnostics.accessible = False
            return None
        diagnostics.accessible = True
        return cls(descriptor)

    def close(self) -> None:
        if self._descriptor >= 0:
            os.close(self._descriptor)
            self._descriptor = -1

    def __enter__(self) -> SecureDirectory:
        return self

    def __exit__(self, *_unused: object) -> None:
        self.close()

    def open_directory(self, component: str, diagnostics: SourceDiagnostics) -> SecureDirectory | None:
        if not valid_component(component):
            diagnostics.issues[CollectionIssue.INVALID_VALUE] += 1
            return None
        try:
            descriptor = os.open(
                component,
                os.O_RDONLY | os.O_CLOEXEC | os.O_DIRECTORY | os.O_NOFOLLOW,
                dir_fd=self._descriptor,
            )
        except FileNotFoundError:
            return None
        except OSError as error:
            _record_os_error(error, diagnostics)
            return None
        return SecureDirectory(descriptor)

    def read_text(
        self,
        name: str,
        maximum_size: int,
        diagnostics: SourceDiagnostics,
    ) -> str | None:
        if not valid_component(name, 255):
            diagnostics.issues[CollectionIssue.INVALID_VALUE] += 1
            return None
        try:
            descriptor = os.open(
                name,
                os.O_RDONLY | os.O_CLOEXEC | os.O_NOFOLLOW | os.O_NONBLOCK,
                dir_fd=self._descriptor,
            )
        except FileNotFoundError:
            return None
        except OSError as error:
            _record_os_error(error, diagnostics)
            return None

        try:
            status = os.fstat(descriptor)
            if not stat.S_ISREG(status.st_mode) or status.st_nlink != 1:
                diagnostics.issues[CollectionIssue.INVALID_VALUE] += 1
                return None
            if status.st_size < 0 or status.st_size > maximum_size:
                diagnostics.issues[CollectionIssue.OVERSIZED_RECORD] += 1
                return None
            while True:
                try:
                    content = os.read(descriptor, maximum_size + 1)
                    break
                except InterruptedError:
                    continue
        except OSError as error:
            _record_os_error(error, diagnostics)
            return None
        finally:
            os.close(descriptor)

        if len(content) > maximum_size:
            diagnostics.issues[CollectionIssue.OVERSIZED_RECORD] += 1
            return None
        diagnostics.files_read += 1
        try:
            value = content.decode("utf-8").strip(" \t\n\r\v\f")
        except UnicodeDecodeError:
            diagnostics.issues[CollectionIssue.INVALID_VALUE] += 1
            return None
        if not value:
            diagnostics.issues[CollectionIssue.INVALID_VALUE] += 1
            return None
        return value

    def entries(self, maximum_entries: int, diagnostics: SourceDiagnostics) -> list[str]:
        try:
            names = os.listdir(self._descriptor)
        except OSError as error:
            _record_os_error(error, diagnostics)
            return []
        if len(names) > maximum_entries:
            diagnostics.issues[CollectionIssue.CARDINALITY_LIMIT] += 1
            return names[:maximum_entries]
        return names

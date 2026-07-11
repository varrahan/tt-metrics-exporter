"""Bounded, diagnostic-aware readers for telemetry sysfs attributes."""

from __future__ import annotations

import errno
from pathlib import Path
import stat
from typing import Callable, TypeVar

from ..models import CollectionIssue, SourceDiagnostics
from .parsers import parse_boolish_int, parse_byte_value, parse_int64, parse_uint64, tokenize


_MAXIMUM_SYSFS_FILE_BYTES = 64 * 1024
_Value = TypeVar("_Value")


class SysfsReader:
    @staticmethod
    def _read_text(path: Path, diagnostics: SourceDiagnostics) -> str | None:
        try:
            status = path.stat()
            if not stat.S_ISREG(status.st_mode):
                diagnostics.issues[CollectionIssue.INVALID_VALUE] += 1
                return None
            if status.st_size > _MAXIMUM_SYSFS_FILE_BYTES:
                diagnostics.issues[CollectionIssue.OVERSIZED_RECORD] += 1
                return None
            with path.open("rb") as source:
                content = source.read(_MAXIMUM_SYSFS_FILE_BYTES + 1)
        except FileNotFoundError:
            return None
        except PermissionError:
            diagnostics.issues[CollectionIssue.PERMISSION_DENIED] += 1
            return None
        except OSError as error:
            issue = CollectionIssue.PERMISSION_DENIED if error.errno in {errno.EACCES, errno.EPERM} else CollectionIssue.READ_FAILED
            diagnostics.issues[issue] += 1
            return None
        if len(content) > _MAXIMUM_SYSFS_FILE_BYTES:
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

    def _read_first(self, root: Path, names: tuple[str, ...], diagnostics: SourceDiagnostics, reader: Callable[[Path, SourceDiagnostics], _Value | None]) -> _Value | None:
        for name in names:
            value = reader(root / name, diagnostics)
            if value is not None:
                return value
        return None

    def _read_first_text(self, root: Path, names: tuple[str, ...], diagnostics: SourceDiagnostics) -> str | None:
        return self._read_first(root, names, diagnostics, self._read_text)

    def _read_int(self, path: Path, diagnostics: SourceDiagnostics) -> int | None:
        return self._parse(path, diagnostics, parse_int64)

    def _read_uint(self, path: Path, diagnostics: SourceDiagnostics) -> int | None:
        return self._parse(path, diagnostics, parse_uint64)

    def _read_boolish(self, path: Path, diagnostics: SourceDiagnostics) -> int | None:
        return self._parse(path, diagnostics, parse_boolish_int)

    def _parse(self, path: Path, diagnostics: SourceDiagnostics, parser: Callable[[str], int | None]) -> int | None:
        value = self._read_text(path, diagnostics)
        if value is None:
            return None
        parsed = parser(value)
        if parsed is None:
            diagnostics.issues[CollectionIssue.INVALID_VALUE] += 1
        return parsed

    def _read_first_int(self, root: Path, names: tuple[str, ...], diagnostics: SourceDiagnostics) -> int | None:
        return self._read_first(root, names, diagnostics, self._read_int)

    def _read_first_boolish(self, root: Path, names: tuple[str, ...], diagnostics: SourceDiagnostics) -> int | None:
        return self._read_first(root, names, diagnostics, self._read_boolish)

    def _read_byte(self, path: Path, diagnostics: SourceDiagnostics) -> int | None:
        value = self._read_text(path, diagnostics)
        if value is None:
            return None
        tokens = tokenize(value)
        if not tokens:
            return None
        parsed = parse_byte_value(tokens[0], tokens[1] if len(tokens) > 1 else "")
        if parsed is None:
            diagnostics.issues[CollectionIssue.INVALID_VALUE] += 1
        return parsed

    def _read_first_byte(self, root: Path, names: tuple[str, ...], diagnostics: SourceDiagnostics) -> int | None:
        return self._read_first(root, names, diagnostics, self._read_byte)

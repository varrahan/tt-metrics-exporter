"""Pure parsers for bounded telemetry input formats."""

from __future__ import annotations

from decimal import Decimal, InvalidOperation
import re

from ..models import MemoryUsage, PciResource


_UINT64_MAX = (1 << 64) - 1
_INT64_MIN = -(1 << 63)
_INT64_MAX = (1 << 63) - 1
_NUMBER_PREFIX = re.compile(
    r"^[+-]?(?:(?:\d+(?:\.\d*)?)|(?:\.\d+))(?:[eE][+-]?\d+)?"
)
_BYTE_MULTIPLIERS = {
    "": 1,
    "b": 1,
    "byte": 1,
    "bytes": 1,
    "k": 1 << 10,
    "kb": 1 << 10,
    "kib": 1 << 10,
    "m": 1 << 20,
    "mb": 1 << 20,
    "mib": 1 << 20,
    "g": 1 << 30,
    "gb": 1 << 30,
    "gib": 1 << 30,
    "t": 1 << 40,
    "tb": 1 << 40,
    "tib": 1 << 40,
}


def tokenize(value: str) -> list[str]:
    """Match the exporter's ASCII sysfs tokenization rules."""

    normalized = "".join(
        character
        if (character.isascii() and character.isalnum())
        or character in "._-"
        else " "
        for character in value
    )
    return normalized.split()


def parse_byte_value(token: str, next_token: str = "") -> int | None:
    match = _NUMBER_PREFIX.match(token)
    if match is None:
        return None
    try:
        value = Decimal(match.group())
    except InvalidOperation:
        return None
    if not value.is_finite() or value < 0:
        return None

    suffix = token[match.end():] or next_token
    multiplier = _BYTE_MULTIPLIERS.get(suffix.lower())
    if multiplier is None:
        if match.end() != len(token):
            return None
        multiplier = 1
    parsed = int(value * multiplier)
    return parsed if parsed <= _UINT64_MAX else None


def parse_uint64(value: str) -> int | None:
    text = value.strip()
    if not text:
        return None
    sign = -1 if text.startswith("-") else 1
    digits = text[1:] if text[:1] in "+-" else text
    if not digits:
        return None
    if digits.lower().startswith("0x"):
        base = 16
    elif len(digits) > 1 and digits[0] == "0":
        base = 8
    else:
        base = 10
    if base == 16:
        digits = digits[2:]
    try:
        parsed = sign * int(digits, base)
    except ValueError:
        return None
    if parsed < 0:
        return parsed % (1 << 64)
    return min(parsed, _UINT64_MAX)


def parse_int64(value: str) -> int | None:
    text = value.strip()
    if re.fullmatch(r"[+-]?\d+", text) is None:
        return None
    parsed = int(text)
    return min(max(parsed, _INT64_MIN), _INT64_MAX)


def parse_boolish_int(value: str) -> int | None:
    normalized = value.strip().lower()
    if normalized in {"true", "yes", "required", "needed"}:
        return 1
    if normalized in {"false", "no", "not_required", "none"}:
        return 0
    return parse_int64(normalized)


def parse_mesh_dimensions(value: str) -> tuple[int, int] | None:
    normalized = value.strip().lower()
    if "x" in normalized:
        rows_text, columns_text = normalized.split("x", 1)
        rows = parse_int64(rows_text)
        columns = parse_int64(columns_text)
        if rows is not None and columns is not None:
            return rows, columns
    tokens = tokenize(value)
    if len(tokens) < 2:
        return None
    rows = parse_int64(tokens[0])
    columns = parse_int64(tokens[1])
    return None if rows is None or columns is None else (rows, columns)


def parse_memory_usage(content: str) -> MemoryUsage:
    usage = MemoryUsage()
    unkeyed_values: list[int] = []
    for line in content.splitlines():
        tokens = tokenize(line)
        for index, token in enumerate(tokens):
            key = token.lower()
            following = tokens[index + 1] if index + 1 < len(tokens) else ""
            unit = tokens[index + 2] if index + 2 < len(tokens) else ""
            if "used" in key or "allocated" in key:
                parsed = parse_byte_value(following, unit)
                if parsed is not None:
                    usage.used_bytes = parsed
                continue
            if "total" in key or "capacity" in key or key == "size":
                parsed = parse_byte_value(following, unit)
                if parsed is not None:
                    usage.total_bytes = parsed
                continue
            parsed = parse_byte_value(token, following)
            if parsed is not None:
                unkeyed_values.append(parsed)

    if usage.used_bytes is None and unkeyed_values:
        usage.used_bytes = unkeyed_values[0]
    if usage.total_bytes is None and len(unkeyed_values) >= 2:
        usage.total_bytes = unkeyed_values[1]
    return usage


def parse_pci_resources(content: str) -> list[PciResource]:
    resources: list[PciResource] = []
    for index, line in enumerate(content.splitlines()):
        fields = line.split()
        if len(fields) < 3:
            continue
        start, end, flags = (parse_uint64(value) for value in fields[:3])
        if start is None or end is None or flags is None:
            continue
        if start == end == flags == 0 or end < start:
            continue
        resources.append(
            PciResource(
                index=index,
                start=start,
                end=end,
                flags=flags,
                size_bytes=end - start + 1,
            )
        )
    return resources

#!/usr/bin/env python3

from pathlib import Path
import random
import unittest

from tt_metrics_exporter.models import (
    CollectionIssue,
    CollectionResult,
    DeviceTelemetry,
)
from tt_metrics_exporter.collection.parsers import parse_memory_usage, parse_pci_resources


CORPUS = Path(__file__).parents[1] / "fixtures/corpus"


class ModelTest(unittest.TestCase):
    def test_defaults_are_independent_and_complete(self) -> None:
        first = DeviceTelemetry(id="0")
        second = DeviceTelemetry(id="1")
        first.pci_resources.append(parse_pci_resources("0x1 0x1 0x1")[0])
        self.assertEqual(second.pci_resources, [])

        result = CollectionResult()
        self.assertEqual(len(result.sources), 4)
        for diagnostics in result.sources.values():
            self.assertEqual(set(diagnostics.issues), set(CollectionIssue))


class ParserContractTest(unittest.TestCase):
    def test_memory_corpus_and_examples(self) -> None:
        cases = [
            ("", (None, None)),
            ("used_bytes: 1024\ntotal_bytes: 4096\n", (1024, 4096)),
            ("2048 8192\n", (2048, 8192)),
            ("used: 1.5 GiB\ntotal: 3 TiB\n", (1610612736, 3298534883328)),
            ("used: 1e100 GiB\n", (None, None)),
            ((CORPUS / "memory/regression-units.txt").read_text(),
             (1610612736, 18446744073709551615)),
        ]
        for content, expected in cases:
            parsed = parse_memory_usage(content)
            self.assertEqual((parsed.used_bytes, parsed.total_bytes), expected)

    def test_pci_corpus_and_examples(self) -> None:
        self.assertEqual(parse_pci_resources(""), [])
        resource = parse_pci_resources(
            "0x800000000 0x81fffffff 0x140204\n0x0 0x0 0x0\n"
        )[0]
        self.assertEqual(
            (resource.index, resource.start, resource.end, resource.flags, resource.size_bytes),
            (0, 34359738368, 34896609279, 1311236, 536870912),
        )
        overflow = parse_pci_resources(
            (CORPUS / "pci/regression-overflow.txt").read_text()
        )
        self.assertEqual(len(overflow), 1)
        self.assertEqual(overflow[0].size_bytes, 1)

    def test_deterministic_parser_fuzz_smoke(self) -> None:
        randomizer = random.Random(0x54544D4554524943)
        alphabet = "0123456789abcdefxABCDEF ._:-\n\tkKmMgGtTiIbBusedtotal"
        for _ in range(250):
            content = "".join(
                randomizer.choice(alphabet) for _ in range(randomizer.randrange(200))
            )
            self.assertEqual(parse_memory_usage(content), parse_memory_usage(content))
            self.assertEqual(parse_pci_resources(content), parse_pci_resources(content))


if __name__ == "__main__":
    unittest.main()

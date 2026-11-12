import math
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from dsd_filter import cic_coefficients, compensation, quantize, response


class DsdFilterTest(unittest.TestCase):
    def test_quantized_filters_preserve_dc_symmetry_and_accumulator_headroom(self):
        for ratio in (1, 2, 4):
            for coefficients, bits in ((cic_coefficients(ratio), 28), (compensation(ratio), 30)):
                values = quantize(coefficients, bits)
                self.assertEqual(1 << bits, sum(values))
                self.assertEqual(values, values[::-1])
                self.assertLess(sum(abs(value) for value in values), 2 * (1 << bits))

    def test_passband_and_every_first_stage_alias_into_the_audio_band(self):
        for ratio in (1, 2, 4):
            rate = 2822400 * ratio
            first = cic_coefficients(ratio)
            second = [value / 2 ** 30 for value in quantize(compensation(ratio), 30)]
            ripple = max(abs(20 * math.log10(abs(response(first, frequency / rate)
                                                * response(second, frequency / 176400))))
                         for frequency in range(0, 20001, 100))
            self.assertLess(ripple, 0.01)
            aliases = [abs(response(first, frequency / rate))
                       for centre in range(176400, rate // 2 + 1, 176400)
                       for frequency in range(centre - 20000, min(centre + 20000, rate // 2) + 1, 1000)]
            self.assertLess(max(aliases), 10 ** (-85 / 20))
        stop = max(abs(response(second, frequency / 176400))
                   for frequency in range(44100, 88201, 100))
        self.assertLess(stop, 10 ** (-85 / 20))

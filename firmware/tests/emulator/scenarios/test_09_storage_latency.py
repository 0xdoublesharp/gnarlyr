import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from echo_image_harness import (
    EchoImageHarness,
    main,
)


class EchoImageTest(EchoImageHarness):
    def test_internal_storage_is_read_with_multiple_block_commands_that_are_stopped(self):
        session = self.session()
        session.run_to_home_screen()
        indexes = [index for _, index, _ in session.card.commands]
        self.assertIn(18, indexes)
        for position, index in enumerate(indexes[:-1]):
            if index == 18:
                self.assertEqual(indexes[position + 1], 12)

    def test_a_cold_boot_on_a_3_4_gib_volume_lights_the_screen_and_serves_usb_quickly(self):
        self.realistic_boot(8, None, 1500, 3000)

    def test_a_cold_boot_with_512_byte_clusters_and_a_stale_hint_stays_responsive(self):
        self.realistic_boot(1, 2, 5000, 6000)


if __name__ == "__main__":
    sys.exit(main(EchoImageTest))

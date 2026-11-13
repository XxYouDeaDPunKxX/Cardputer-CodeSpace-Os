"""Small real lock/date tests; run with system Python or the runtime venv."""
from contextlib import contextmanager
from datetime import datetime, timedelta, timezone
import importlib
from pathlib import Path
import tempfile
import threading
import unittest

import port_keeper as keeper


class ControlTest(unittest.TestCase):
    def test_pause_waits_until_publication_transaction_finishes(self):
        original_base, original_control = keeper.BASE, keeper.CONTROL
        with tempfile.TemporaryDirectory() as directory:
            keeper.BASE = Path(directory)
            keeper.CONTROL = keeper.BASE / 'control.json'
            keeper.save_control({'enabled': True, 'token_expires': '2099-01-01'})
            entered, release, paused = threading.Event(), threading.Event(), threading.Event()
            order = []

            def publication():
                with keeper.control_lock():
                    entered.set()
                    release.wait(2)
                    order.append('publication-finished')

            def pause():
                with keeper.control_lock():
                    keeper.save_control({'enabled': False})
                    order.append('pause-saved')
                    paused.set()

            first = threading.Thread(target=publication)
            second = threading.Thread(target=pause)
            first.start()
            self.assertTrue(entered.wait(1))
            second.start()
            try:
                self.assertFalse(paused.wait(.2), 'pause overtook in-flight publication')
            finally:
                release.set()
                first.join(3)
                second.join(3)
                keeper.BASE, keeper.CONTROL = original_base, original_control
            self.assertEqual(order, ['publication-finished', 'pause-saved'])

    def test_expired_or_missing_policy_fails_closed(self):
        today = datetime.now(timezone.utc).date()
        self.assertEqual(keeper.days_remaining({'token_expires': (today-timedelta(days=1)).isoformat()}), -1)
        self.assertEqual(keeper.days_remaining({'token_expires': today.isoformat()}), 0)
        self.assertEqual(keeper.days_remaining({'token_expires': (today+timedelta(days=1)).isoformat()}), 1)
        self.assertEqual(keeper.days_remaining({}), -1)


if __name__ == '__main__':
    import sys
    if '--without-lock' in sys.argv:
        sys.argv.remove('--without-lock')

        @contextmanager
        def unlocked():
            yield

        keeper.control_lock = unlocked
    unittest.main()

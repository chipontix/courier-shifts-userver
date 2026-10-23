"""Generates Pandora http/json ammo for the seeded data set (see seed.sql).

Mix: 60% earnings reads, 20% shift listings, 15% new deliveries, 5% new shifts.
Writes are unique within one pass of the file; once Pandora loops the file,
repeated writes get 409, which report.py counts as a correct business answer.
"""

import json
import random
import sys
from datetime import datetime, timedelta, timezone

COURIERS = 10_000
DAYS = 14
SEED_START = datetime(2026, 10, 1, tzinfo=timezone.utc)
FUTURE_START = datetime(2026, 11, 1, tzinfo=timezone.utc)
HEADERS = {'Content-Type': 'application/json', 'Connection': 'keep-alive'}


def iso(dt):
    return dt.strftime('%Y-%m-%dT%H:%M:%SZ')


def main(count, out):
    rnd = random.Random(42)
    for seq in range(count):
        courier = rnd.randint(1, COURIERS)
        day = rnd.randrange(DAYS)
        roll = rnd.random()
        if roll < 0.60:
            start = SEED_START + timedelta(days=day)
            ammo = {
                'tag': 'earnings',
                'method': 'GET',
                'uri': f'/v1/couriers/{courier}/earnings?from={iso(start)}&to={iso(start + timedelta(days=1))}',
            }
        elif roll < 0.80:
            start = SEED_START + timedelta(days=rnd.randrange(DAYS - 7))
            ammo = {
                'tag': 'shifts',
                'method': 'GET',
                'uri': f'/v1/couriers/{courier}/shifts?from={iso(start)}&to={iso(start + timedelta(days=7))}',
            }
        elif roll < 0.95:
            shift_id = (courier - 1) * DAYS + day + 1
            delivered = SEED_START + timedelta(days=day, hours=8, seconds=rnd.randrange(12 * 3600))
            ammo = {
                'tag': 'assign',
                'method': 'POST',
                'uri': f'/v1/shifts/{shift_id}/assignments',
                'body': json.dumps(
                    {'order_id': f'load-{seq}', 'distance_m': rnd.randint(200, 8000), 'delivered_at': iso(delivered)},
                ),
            }
        else:
            start = FUTURE_START + timedelta(days=rnd.randrange(365), hours=rnd.randint(6, 14))
            ammo = {
                'tag': 'create_shift',
                'method': 'POST',
                'uri': '/v1/shifts',
                'body': json.dumps(
                    {
                        'courier_id': courier,
                        'zone': 'north',
                        'starts_at': iso(start),
                        'ends_at': iso(start + timedelta(hours=4)),
                    },
                ),
            }
        ammo['headers'] = HEADERS
        ammo['host'] = 'service'
        out.write(json.dumps(ammo) + '\n')


if __name__ == '__main__':
    main(int(sys.argv[1]), sys.stdout)

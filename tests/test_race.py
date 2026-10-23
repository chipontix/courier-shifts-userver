import asyncio

ROUNDS = 10
CONCURRENCY = 16


async def test_concurrent_overlapping_shifts_admit_exactly_one(service_client, pgsql):
    """Many overlapping shifts for one courier race; at most one may win."""
    for round_no in range(ROUNDS):
        response = await service_client.post('/v1/couriers', json={'name': f'racer-{round_no}'})
        courier_id = response.json()['id']

        # Every window overlaps every other one: all of them contain 11:00-12:00.
        bodies = [
            {
                'courier_id': courier_id,
                'zone': 'north',
                'starts_at': f'2026-10-20T{9 + i % 2:02}:{(i * 7) % 60:02}:00Z',
                'ends_at': f'2026-10-20T{12 + i % 3:02}:00:00Z',
            }
            for i in range(CONCURRENCY)
        ]
        responses = await asyncio.gather(
            *(service_client.post('/v1/shifts', json=body) for body in bodies),
        )
        statuses = sorted(r.status for r in responses)
        assert statuses == [201] + [409] * (CONCURRENCY - 1), f'round {round_no}: {statuses}'

        cursor = pgsql['db'].cursor()
        cursor.execute(
            'SELECT count(*) FROM courier_shifts.shifts WHERE courier_id = %s AND cancelled_at IS NULL',
            (courier_id,),
        )
        assert cursor.fetchone()[0] == 1, f'round {round_no}'

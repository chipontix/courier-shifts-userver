import pytest

DAY = '2026-10-20'


def ts(hhmm, day=DAY):
    return f'{day}T{hhmm}:00Z'


async def create_courier(service_client, name='Aigerim'):
    response = await service_client.post('/v1/couriers', json={'name': name})
    assert response.status == 201
    return response.json()['id']


async def create_shift(service_client, courier_id, start, end, zone='north'):
    return await service_client.post(
        '/v1/shifts',
        json={
            'courier_id': courier_id,
            'zone': zone,
            'starts_at': start,
            'ends_at': end,
        },
    )


async def assign(service_client, shift_id, order_id, distance_m, delivered_at):
    return await service_client.post(
        f'/v1/shifts/{shift_id}/assignments',
        json={
            'order_id': order_id,
            'distance_m': distance_m,
            'delivered_at': delivered_at,
        },
    )


async def earnings(service_client, courier_id, day=DAY):
    response = await service_client.get(
        f'/v1/couriers/{courier_id}/earnings',
        params={'from': ts('00:00', day), 'to': ts('23:59', day)},
    )
    assert response.status == 200
    return response.json()


async def test_create_and_list_shifts(service_client):
    courier_id = await create_courier(service_client)
    response = await create_shift(service_client, courier_id, ts('09:00'), ts('13:00'))
    assert response.status == 201
    shift_id = response.json()['id']

    response = await service_client.get(
        f'/v1/couriers/{courier_id}/shifts',
        params={'from': ts('00:00'), 'to': ts('23:59')},
    )
    assert response.status == 200
    assert response.json() == {
        'shifts': [
            {
                'id': shift_id,
                'zone': 'north',
                'starts_at': '2026-10-20T09:00:00+00:00',
                'ends_at': '2026-10-20T13:00:00+00:00',
                'cancelled': False,
            },
        ],
    }


async def test_overlap_rejected_adjacent_allowed(service_client):
    courier_id = await create_courier(service_client)
    assert (await create_shift(service_client, courier_id, ts('09:00'), ts('13:00'))).status == 201
    assert (await create_shift(service_client, courier_id, ts('12:00'), ts('14:00'))).status == 409
    # [09:00, 13:00) and [13:00, 15:00) touch but do not overlap
    assert (await create_shift(service_client, courier_id, ts('13:00'), ts('15:00'))).status == 201
    # another courier is independent
    other = await create_courier(service_client, 'Dauren')
    assert (await create_shift(service_client, other, ts('09:00'), ts('13:00'))).status == 201


async def test_cancelled_shift_frees_the_slot(service_client):
    courier_id = await create_courier(service_client)
    shift_id = (await create_shift(service_client, courier_id, ts('09:00'), ts('13:00'))).json()['id']
    response = await service_client.post(f'/v1/shifts/{shift_id}/cancel')
    assert response.status == 200
    assert (await create_shift(service_client, courier_id, ts('10:00'), ts('11:00'))).status == 201


@pytest.mark.parametrize(
    'body, status',
    [
        ({'courier_id': 'x', 'zone': 'a', 'starts_at': ts('09:00'), 'ends_at': ts('10:00')}, 400),
        ({'courier_id': 1, 'zone': '', 'starts_at': ts('09:00'), 'ends_at': ts('10:00')}, 400),
        ({'courier_id': 1, 'zone': 'a', 'starts_at': 'yesterday', 'ends_at': ts('10:00')}, 400),
        ({'courier_id': 1, 'zone': 'a', 'starts_at': ts('10:00'), 'ends_at': ts('10:00')}, 400),
        ({'courier_id': 1, 'zone': 'a', 'starts_at': ts('00:00'), 'ends_at': ts('13:00')}, 400),
        ({'courier_id': 999999, 'zone': 'a', 'starts_at': ts('09:00'), 'ends_at': ts('10:00')}, 404),
    ],
)
async def test_create_shift_validation(service_client, body, status):
    response = await service_client.post('/v1/shifts', json=body)
    assert response.status == status


async def test_assignments(service_client):
    courier_id = await create_courier(service_client)
    shift_id = (await create_shift(service_client, courier_id, ts('09:00'), ts('13:00'))).json()['id']

    assert (await assign(service_client, shift_id, 'o-1', 2500, ts('10:00'))).status == 201
    # same order twice
    assert (await assign(service_client, shift_id, 'o-1', 2500, ts('10:30'))).status == 409
    # delivered after the shift ended ([09:00, 13:00) excludes 13:00)
    assert (await assign(service_client, shift_id, 'o-2', 100, ts('13:00'))).status == 409
    # unknown shift
    assert (await assign(service_client, 999999, 'o-3', 100, ts('10:00'))).status == 404
    assert (await assign(service_client, shift_id, 'o-4', -1, ts('10:00'))).status == 400


async def test_earnings_default_tariffs(service_client):
    courier_id = await create_courier(service_client)
    north = (await create_shift(service_client, courier_id, ts('09:00'), ts('13:30'))).json()['id']
    center = (
        await create_shift(service_client, courier_id, ts('15:00'), ts('16:00'), zone='center')
    ).json()['id']
    assert (await assign(service_client, north, 'o-1', 2500, ts('10:00'))).status == 201
    assert (await assign(service_client, north, 'o-2', 1333, ts('11:00'))).status == 201
    assert (await assign(service_client, center, 'o-3', 1000, ts('15:10'))).status == 201

    # defaults: 250.00/h, 90.00/delivery, 15.00/km, center x1.2, everything rounded down
    # north: 4.5h -> 112500; 2 deliveries -> 18000 + 3833m * 15.00/km = 5749 -> 23749
    # center: 1h -> 25000; 9000 + 1500 = 10500; (25000 + 10500) * 120% = 42600
    assert await earnings(service_client, courier_id) == {
        'courier_id': courier_id,
        'tariffs_version': 1,
        'total_cents': 112500 + 23749 + 42600,
        'shifts': [
            {'shift_id': north, 'time_cents': 112500, 'deliveries_cents': 23749, 'total_cents': 136249},
            {'shift_id': center, 'time_cents': 25000, 'deliveries_cents': 10500, 'total_cents': 42600},
        ],
    }


async def test_earnings_cache_invalidated_by_writes(service_client, redis_standalone_store):
    courier_id = await create_courier(service_client)
    shift_id = (await create_shift(service_client, courier_id, ts('09:00'), ts('10:00'))).json()['id']

    first = await earnings(service_client, courier_id)
    assert first['total_cents'] == 25000
    assert redis_standalone_store.exists(f'earnings:{courier_id}')

    assert (await assign(service_client, shift_id, 'o-1', 0, ts('09:30'))).status == 201
    assert not redis_standalone_store.exists(f'earnings:{courier_id}')
    assert (await earnings(service_client, courier_id))['total_cents'] == 34000

    assert (await service_client.post(f'/v1/shifts/{shift_id}/cancel')).status == 200
    assert (await earnings(service_client, courier_id))['total_cents'] == 0


async def test_earnings_served_from_cache(service_client, redis_standalone_store):
    courier_id = await create_courier(service_client)
    await create_shift(service_client, courier_id, ts('09:00'), ts('10:00'))
    assert (await earnings(service_client, courier_id))['total_cents'] == 25000

    # Overwrite the cached entry: a second read must return it without touching the database.
    key = f'earnings:{courier_id}'
    (field,) = redis_standalone_store.hkeys(key)
    redis_standalone_store.hset(key, field, '{"total_cents": 1}')
    assert (await earnings(service_client, courier_id)) == {'total_cents': 1}


@pytest.mark.config(
    COURIER_TARIFFS={
        'version': 7,
        'per_hour_cents': 100000,
        'per_delivery_cents': 0,
        'per_km_cents': 0,
        'zone_multiplier_pct': {'north': 50},
    },
)
async def test_earnings_follow_dynamic_config(service_client):
    courier_id = await create_courier(service_client)
    await create_shift(service_client, courier_id, ts('09:00'), ts('11:00'))
    result = await earnings(service_client, courier_id)
    assert result['tariffs_version'] == 7
    assert result['total_cents'] == 100000


@pytest.mark.config(COURIER_MAX_SHIFT_HOURS=2)
async def test_max_shift_duration_from_dynamic_config(service_client):
    courier_id = await create_courier(service_client)
    assert (await create_shift(service_client, courier_id, ts('09:00'), ts('11:30'))).status == 400
    assert (await create_shift(service_client, courier_id, ts('09:00'), ts('11:00'))).status == 201

-- 10 000 couriers, one 12h shift per courier per day for 14 days, 10
-- deliveries per shift. Shift ids are (courier_id - 1) * 14 + day + 1, which
-- gen_ammo.py relies on (checked at the end).
INSERT INTO courier_shifts.couriers (name)
SELECT 'courier-' || c FROM generate_series(1, 10000) c ORDER BY c;

INSERT INTO courier_shifts.shifts (courier_id, zone, period)
SELECT c,
       (ARRAY['north', 'south', 'center', 'east'])[1 + c % 4],
       tstzrange(timestamptz '2026-10-01 08:00Z' + d * interval '1 day',
                 timestamptz '2026-10-01 20:00Z' + d * interval '1 day', '[)')
FROM generate_series(1, 10000) c, generate_series(0, 13) d
ORDER BY c, d;

INSERT INTO courier_shifts.assignments (shift_id, order_id, distance_m, delivered_at)
SELECT s.id, 'seed-' || s.id || '-' || k, 500 + (s.id * 7 + k * 131) % 6000,
       lower(s.period) + k * interval '1 hour' + interval '5 min'
FROM courier_shifts.shifts s, generate_series(0, 9) k;

ANALYZE;

DO $$
BEGIN
  IF (SELECT courier_id * 100 + extract(day FROM lower(period))
      FROM courier_shifts.shifts WHERE id = 14 * 4242 + 7 + 1) <> 4243 * 100 + 8 THEN
    RAISE EXCEPTION 'unexpected shift id layout';
  END IF;
END $$;

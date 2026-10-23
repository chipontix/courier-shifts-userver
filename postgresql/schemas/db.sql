CREATE EXTENSION IF NOT EXISTS btree_gist;  -- `courier_id WITH =` inside a GiST exclusion constraint
CREATE SCHEMA IF NOT EXISTS courier_shifts;

CREATE TABLE courier_shifts.couriers (
    id         BIGSERIAL PRIMARY KEY,
    name       TEXT        NOT NULL,
    created_at TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE TABLE courier_shifts.shifts (
    id           BIGSERIAL PRIMARY KEY,
    courier_id   BIGINT      NOT NULL REFERENCES courier_shifts.couriers (id),
    zone         TEXT        NOT NULL,
    -- half-open [start, end): a shift ending at 12:00 and one starting at 12:00 do not overlap
    period       TSTZRANGE   NOT NULL CHECK (NOT isempty(period) AND lower_inc(period) AND NOT upper_inc(period)),
    cancelled_at TIMESTAMPTZ,
    -- The invariant lives here, not in the handler: a check-then-insert in
    -- application code lets concurrent requests both pass (tests/test_race.py).
    CONSTRAINT shifts_no_overlap EXCLUDE USING gist (courier_id WITH =, period WITH &&)
        WHERE (cancelled_at IS NULL)
);
CREATE INDEX shifts_courier_start_idx ON courier_shifts.shifts (courier_id, lower(period));

CREATE TABLE courier_shifts.assignments (
    id           BIGSERIAL PRIMARY KEY,
    shift_id     BIGINT      NOT NULL REFERENCES courier_shifts.shifts (id),
    order_id     TEXT        NOT NULL UNIQUE,
    distance_m   INTEGER     NOT NULL CHECK (distance_m >= 0),
    delivered_at TIMESTAMPTZ NOT NULL
);
CREATE INDEX assignments_shift_idx ON courier_shifts.assignments (shift_id);

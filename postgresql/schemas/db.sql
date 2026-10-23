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
    cancelled_at TIMESTAMPTZ
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

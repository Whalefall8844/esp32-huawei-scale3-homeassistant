"""Persistent per-person archive; no Home Assistant dependency. GPL-3.0-or-later."""
from datetime import datetime, timedelta
import json
import math
from pathlib import Path
import sqlite3
from zoneinfo import ZoneInfo

from .model import calculate
from statistics import median


class Archive:
    def __init__(self, path, profiles, weight_tolerance=3.0, impedance_tolerance=100.0,
                 initial_references=None, time_zone="UTC"):
        self.path = Path(path)
        self.profiles = profiles
        self.time_zone = ZoneInfo(time_zone)
        self.weight_tolerance = weight_tolerance
        self.impedance_tolerance = impedance_tolerance
        self.initial_references = initial_references or {}

    def _connect(self):
        self.path.parent.mkdir(parents=True, exist_ok=True)
        db = sqlite3.connect(self.path)
        db.row_factory = sqlite3.Row
        db.execute("CREATE TABLE IF NOT EXISTS records (id TEXT PRIMARY KEY, measured_at TEXT NOT NULL, "
                   "person TEXT, reason TEXT NOT NULL, sample TEXT NOT NULL, metrics TEXT NOT NULL)")
        db.execute("CREATE TABLE IF NOT EXISTS settings (name TEXT PRIMARY KEY, value TEXT NOT NULL)")
        return db

    def _activity(self, db, person):
        row = db.execute("SELECT value FROM settings WHERE name=?", (person + '_activity',)).fetchone()
        return float(row[0]) if row else 1.2

    def _reference(self, db, person):
        rows = db.execute("SELECT sample FROM records WHERE person=? ORDER BY julianday(measured_at) DESC, rowid DESC LIMIT 5",
                          (person,)).fetchall()
        if not rows:
            return self.initial_references.get(person)
        samples = [json.loads(row[0]) for row in rows]
        weights = sorted(s['weight'] for s in samples)
        impedances = sorted(s['impedance'] for s in samples if s.get('impedance') is not None)
        return {'weight': median(weights),
                'impedance': median(impedances) if impedances else None}

    def _route(self, db, sample):
        references = {person: self._reference(db, person) for person in self.profiles}
        # All configured profiles need a reference before automatic assignment.
        if any(ref is None for ref in references.values()):
            return None, '首次使用：请认领，建立所有档案的参考值'
        candidates = []
        for person, ref in references.items():
            dw = abs(sample['weight'] - ref['weight']) / self.weight_tolerance
            z, reference_z = sample.get('impedance'), ref.get('impedance')
            dz = abs(z-reference_z)/self.impedance_tolerance if z is not None and reference_z is not None else None
            if dw <= 1 and (dz is None or dz <= 1):
                candidates.append(person)
        if len(candidates) == 1:
            return candidates[0], '体重及可用阻抗唯一匹配'
        return None, '匹配重叠，等待认领' if candidates else '未匹配到档案参考值，等待认领'

    def _metrics(self, db, person, sample, measured_at, activity=None):
        if person is None:
            return {}
        local_date = datetime.fromisoformat(measured_at).astimezone(self.time_zone).date()
        return calculate(self.profiles[person], local_date, sample['weight'], sample.get('impedance'),
                         sample.get('heart_rate'), self._activity(db, person) if activity is None else activity)

    def submit(self, sample, measured_at):
        sample = dict(sample)
        identifier = str(sample['id'])
        if not identifier or len(identifier) > 100:
            raise ValueError('measurement id is required and must be at most 100 characters')
        weight = float(sample['weight'])
        if not math.isfinite(weight) or weight <= 5:
            raise ValueError('invalid weight')
        sample['weight'] = weight
        for key in ('impedance', 'heart_rate'):
            value = sample.get(key)
            if value is not None:
                value = float(value)
                sample[key] = value if math.isfinite(value) and value > 0 else None
        heart = sample.get('heart_rate')
        if heart is not None and not 1 <= heart <= 240:
            sample['heart_rate'] = None
        when = datetime.fromisoformat(measured_at)
        if when.tzinfo is None:
            raise ValueError('measurement timestamp must include timezone')
        db = self._connect()
        try:
            with db:
                old = db.execute('SELECT * FROM records WHERE id=?', (identifier,)).fetchone()
                if old:
                    previous = json.loads(old['sample'])
                    # Optional data may arrive later, but never move an already claimed
                    # record or date on API reconnect / replay of the same cycle id.
                    for key in ('impedance', 'heart_rate'):
                        if sample.get(key) is None:
                            sample[key] = previous.get(key)
                    person, reason, measured_at = old['person'], old['reason'], old['measured_at']
                    # A complete impedance update can resolve a pending cycle.
                    # A claimed record is never reassigned by a later packet.
                    if person is None and sample.get('impedance') is not None and sample['impedance'] != previous.get('impedance'):
                        person, reason = self._route(db, sample)
                else:
                    person, reason = self._route(db, sample)
                activity = json.loads(old['metrics']).get('activity_factor') if old else None
                metrics = self._metrics(db, person, sample, measured_at, activity)
                db.execute('INSERT INTO records VALUES (?,?,?,?,?,?) ON CONFLICT(id) DO UPDATE SET '
                           'person=excluded.person, reason=excluded.reason, sample=excluded.sample, metrics=excluded.metrics',
                           (identifier, measured_at, person, reason, json.dumps(sample), json.dumps(metrics)))
            return self._snapshot(db)
        finally:
            db.close()

    def claim(self, identifier, person):
        if person not in self.profiles:
            raise ValueError('unknown person')
        db = self._connect()
        try:
            with db:
                row = db.execute('SELECT * FROM records WHERE id=?', (identifier,)).fetchone()
                if row is None:
                    raise ValueError('measurement not found')
                sample = json.loads(row['sample'])
                metrics = self._metrics(db, person, sample, row['measured_at'])
                db.execute('UPDATE records SET person=?,reason=?,metrics=? WHERE id=?',
                           (person, '用户认领', json.dumps(metrics), identifier))
            return self._snapshot(db)
        finally:
            db.close()

    def set_activity(self, person, value):
        if person not in self.profiles or not math.isfinite(value) or not 1 <= value <= 2.5:
            raise ValueError('invalid activity factor')
        db = self._connect()
        try:
            with db:
                db.execute('INSERT INTO settings VALUES (?,?) ON CONFLICT(name) DO UPDATE SET value=excluded.value',
                           (person+'_activity', str(value)))
            return self._snapshot(db)
        finally:
            db.close()

    @staticmethod
    def _record(row):
        return dict(id=row['id'], measured_at=row['measured_at'], person=row['person'], reason=row['reason'],
                    sample=json.loads(row['sample']), metrics=json.loads(row['metrics']))

    def _snapshot(self, db):
        latest = {}
        history = {}
        cutoff = (datetime.now(self.time_zone) - timedelta(days=7)).isoformat()
        for person in self.profiles:
            row = db.execute('SELECT * FROM records WHERE person=? ORDER BY julianday(measured_at) DESC, rowid DESC LIMIT 1',
                             (person,)).fetchone()
            latest[person] = self._record(row) if row else None
            rows = db.execute('SELECT id,measured_at,sample,metrics FROM records '
                              'WHERE person=? AND julianday(measured_at)>=julianday(?) '
                              'ORDER BY julianday(measured_at) DESC,rowid DESC LIMIT 1000', (person, cutoff)).fetchall()
            history[person] = [dict(id=r['id'], measured_at=r['measured_at'],
                                   weight=json.loads(r['sample'])['weight'],
                                   body_fat=json.loads(r['metrics']).get('body_fat'))
                               for r in reversed(rows)]
        pending_count = db.execute('SELECT COUNT(*) FROM records WHERE person IS NULL').fetchone()[0]
        pending = [self._record(row) for row in db.execute(
            'SELECT * FROM records WHERE person IS NULL ORDER BY julianday(measured_at) DESC, rowid DESC LIMIT 20')]
        return dict(latest=latest, history=history, pending=pending, pending_count=pending_count,
                    activity={p: self._activity(db, p) for p in self.profiles},
                    references={p:self._reference(db, p) for p in self.profiles})

    def snapshot(self):
        db = self._connect()
        try:
            return self._snapshot(db)
        finally:
            db.close()

"""HA YAML integration for complete scale snapshots. GPL-3.0-or-later.

HA API references: home-assistant/core tag 2026.9.3,
helpers/discovery.py and helpers/event.py. No new network credentials required.
"""
import asyncio
import json
import logging
from pathlib import Path

import voluptuous as vol
from homeassistant.core import callback
from homeassistant.const import EVENT_HOMEASSISTANT_STOP
from homeassistant.exceptions import ServiceValidationError
from homeassistant.helpers import config_validation as cv
from homeassistant.helpers.discovery import async_load_platform
from homeassistant.helpers.event import async_track_state_change_event
from homeassistant.util import dt as dt_util

from .archive import Archive
from zoneinfo import ZoneInfo

DOMAIN = 'huawei_scale_archive'
_LOGGER = logging.getLogger(__name__)
def profile_date(value):
    """YAML may supply either an ISO string or a date object."""
    return cv.date(value).isoformat()


def time_zone(value):
    value = cv.string(value)
    try:
        ZoneInfo(value)
    except (KeyError, ValueError) as exc:
        raise vol.Invalid('time_zone must be an IANA timezone') from exc
    return value


PROFILE_SCHEMA = vol.Schema({
    vol.Required('name'): cv.string,
    vol.Required('sex'): vol.In(('male', 'female')),
    vol.Required('birth_date'): profile_date,
    vol.Required('height_cm'): vol.All(vol.Coerce(float), vol.Range(min=120, max=230)),
})
CONFIG_SCHEMA = vol.Schema({vol.Optional(DOMAIN): vol.Schema({
    vol.Required('source_entity'): cv.entity_id,
    vol.Required('profiles'): vol.All({cv.slug: PROFILE_SCHEMA}, vol.Length(min=1)),
    vol.Optional('time_zone'): time_zone,
    vol.Optional('storage_path', default='huawei_scale_archive.sqlite3'): cv.string,
    vol.Optional('weight_tolerance_kg', default=3.0): vol.All(vol.Coerce(float), vol.Range(min=.1, max=20)),
    vol.Optional('impedance_tolerance_ohm', default=100.0): vol.All(vol.Coerce(float), vol.Range(min=1, max=500)),
    vol.Optional('person_entities', default={}): {cv.slug: cv.entity_id},
    vol.Optional('initial_references', default={}): {cv.slug: vol.Schema({
        vol.Required('weight'): vol.All(vol.Coerce(float), vol.Range(min=20, max=250)),
        vol.Required('impedance'): vol.All(vol.Coerce(float), vol.Range(min=100, max=1000)),
    })},
})}, extra=vol.ALLOW_EXTRA)


class Manager:
    def __init__(self, hass, options):
        self.hass = hass
        self.options = options
        self.profiles = options['profiles']
        self.time_zone = options.get('time_zone', hass.config.time_zone)
        for field in ('person_entities', 'initial_references'):
            unknown = options[field].keys() - self.profiles.keys()
            if unknown:
                raise vol.Invalid(f'{field} contains unknown profile ids: {sorted(unknown)}')
        self.store = Archive(Path(hass.config.path(options['storage_path'])), self.profiles,
                             options['weight_tolerance_kg'], options['impedance_tolerance_ohm'],
                             options['initial_references'], time_zone=self.time_zone)
        self.state = {}
        self.listeners = set()
        self.selected_person = next(iter(self.profiles))
        self.selected_record = None
        self.lock = asyncio.Lock()

    def subscribe(self, listener):
        self.listeners.add(listener)
        return lambda: self.listeners.discard(listener)

    def notify(self):
        ids = [r['id'] for r in self.state['pending']]
        if self.selected_record not in ids:
            self.selected_record = ids[0] if ids else None
        for listener in tuple(self.listeners):
            listener()

    async def call(self, method, *args):
        async with self.lock:
            try:
                self.state = await self.hass.async_add_executor_job(getattr(self.store, method), *args)
            except (ValueError, KeyError, TypeError) as exc:
                raise ServiceValidationError(str(exc)) from exc
            self.notify()

    async def receive(self, event):
        new = event.data.get('new_state')
        if new is None or new.state in ('unknown','unavailable',''):
            return
        try:
            sample = json.loads(new.state)
            # One atomic firmware snapshot. Never combine separate HA sensor states.
            await self.call('submit', sample, dt_util.now().isoformat())
        except (json.JSONDecodeError, ServiceValidationError) as exc:
            _LOGGER.warning('Scale snapshot not archived: %s', exc)


async def async_setup(hass, config):
    if DOMAIN not in config:
        return True
    manager = Manager(hass, config[DOMAIN])
    hass.data[DOMAIN] = manager
    await manager.call('snapshot')

    async def claim(call):
        identifier = call.data.get('measurement_id') or manager.selected_record
        if identifier is None:
            raise ServiceValidationError('没有待认领记录')
        await manager.call('claim', identifier, call.data.get('person_id', manager.selected_person))

    hass.services.async_register(DOMAIN, 'claim', claim, schema=vol.Schema({
        vol.Optional('measurement_id'): cv.string,
        vol.Optional('person_id'): vol.In(manager.profiles),
    }))
    unsubscribe = async_track_state_change_event(hass, config[DOMAIN]['source_entity'], manager.receive)
    hass.bus.async_listen_once(EVENT_HOMEASSISTANT_STOP, lambda event: unsubscribe())
    await asyncio.gather(*(async_load_platform(hass, platform, DOMAIN, {}, config)
                           for platform in ('sensor','number','select','button')))
    # No ingestion of an old existing state on install: its original date is unknown.
    return True


class ListenerMixin:
    _attr_should_poll = False

    async def async_added_to_hass(self):
        await super().async_added_to_hass()
        self.async_on_remove(self.manager.subscribe(self.async_write_ha_state))

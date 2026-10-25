"""Choose an unclaimed record and its owner."""
from datetime import datetime
from homeassistant.components.select import SelectEntity
from . import DOMAIN, ListenerMixin


async def async_setup_platform(hass, config, async_add_entities, discovery_info=None):
    if discovery_info is not None:
        async_add_entities([Choice(hass.data[DOMAIN], kind) for kind in ('person','record')])


class Choice(ListenerMixin, SelectEntity):
    def __init__(self, manager, kind):
        self.manager, self.kind = manager, kind
        self._attr_unique_id = DOMAIN+'_claim_'+kind
        self._attr_name = '称重 '+('归属人' if kind=='person' else '待认领记录')
        self.entity_id = 'select.scale_claim_'+kind

    def _person_label(self, person):
        return f"{self.manager.profiles[person]['name']} [{person}]"

    def _record_options(self):
        return {
            f"记录{index}，{datetime.fromisoformat(record['measured_at']).astimezone(self.manager.store.time_zone):%Y-%m-%d %H:%M:%S}，{record['sample']['weight']:.2f}kg": record['id']
            for index, record in enumerate(self.manager.state['pending'], start=1)
        }

    @property
    def options(self):
        return [self._person_label(p) for p in self.manager.profiles] if self.kind=='person' else (
            list(self._record_options()) or ['无待认领'])

    @property
    def current_option(self):
        return self._person_label(self.manager.selected_person) if self.kind=='person' else (
            next((label for label, identifier in self._record_options().items()
                  if identifier == self.manager.selected_record), '无待认领'))

    async def async_select_option(self, option):
        if self.kind == 'person':
            self.manager.selected_person = next(p for p in self.manager.profiles if self._person_label(p) == option)
        elif option != '无待认领':
            self.manager.selected_record = self._record_options()[option]
        self.manager.notify()

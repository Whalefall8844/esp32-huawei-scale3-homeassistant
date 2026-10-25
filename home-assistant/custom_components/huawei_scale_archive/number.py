"""Activity factors are persisted and applied to future measurements."""
from homeassistant.components.number import NumberEntity, NumberMode
from . import DOMAIN, ListenerMixin


async def async_setup_platform(hass, config, async_add_entities, discovery_info=None):
    if discovery_info is not None:
        async_add_entities([Activity(hass.data[DOMAIN], p) for p in hass.data[DOMAIN].profiles])


class Activity(ListenerMixin, NumberEntity):
    _attr_native_min_value = 1.0
    _attr_native_max_value = 2.5
    _attr_native_step = .05
    _attr_mode = NumberMode.BOX

    def __init__(self, manager, person):
        self.manager, self.person = manager, person
        self._attr_unique_id = f'{DOMAIN}_{person}_activity'
        self._attr_name = manager.profiles[person]['name']+' 活动系数'
        self.entity_id = f'number.scale_{person}_activity'

    @property
    def native_value(self):
        return self.manager.state['activity'][self.person]

    async def async_set_native_value(self, value):
        await self.manager.call('set_activity', self.person, float(value))

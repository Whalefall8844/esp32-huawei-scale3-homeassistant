"""Archive the selected pending measurement under the selected profile."""
from homeassistant.components.button import ButtonEntity
from homeassistant.exceptions import ServiceValidationError
from . import DOMAIN, ListenerMixin


async def async_setup_platform(hass, config, async_add_entities, discovery_info=None):
    if discovery_info is not None:
        async_add_entities([Claim(hass.data[DOMAIN])])


class Claim(ListenerMixin, ButtonEntity):
    def __init__(self, manager):
        self.manager = manager
        self._attr_unique_id = DOMAIN+'_claim'
        self._attr_name = '称重 确认归档'
        self.entity_id = 'button.scale_claim'

    async def async_press(self):
        if self.manager.selected_record is None:
            raise ServiceValidationError('没有待认领记录')
        await self.manager.call('claim', self.manager.selected_record, self.manager.selected_person)

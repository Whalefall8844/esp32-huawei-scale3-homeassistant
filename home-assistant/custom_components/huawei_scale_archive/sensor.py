"""Configured profiles, thirteen metrics each, and pending archive information."""
from datetime import datetime
from homeassistant.components.sensor import SensorEntity, SensorStateClass, SensorDeviceClass
from . import DOMAIN, ListenerMixin
from .model import MODEL_ID, MODEL_COMMIT, MODEL_NAME

METRICS = {
    'weight': ('体重','kg',2), 'bmi': ('BMI',None,1), 'body_fat': ('体脂率','%',1),
    'water': ('水分','%',1), 'skeletal_muscle': ('肌肉','%',1),
    'bmr': ('基础代谢率','kcal/day',0), 'tdee': ('每日总能量消耗','kcal/day',0),
    'visceral_fat': ('内脏脂肪等级',None,0), 'bone_mineral': ('骨骼','kg',2),
    'lean_body_mass': ('去脂体重','kg',2), 'protein': ('蛋白质','%',1),
    'heart_rate': ('心率','bpm',0), 'impedance': ('阻抗','Ω',1),
}


async def async_setup_platform(hass, config, async_add_entities, discovery_info=None):
    if discovery_info is None:
        return
    manager = hass.data[DOMAIN]
    async_add_entities([Metric(manager,p,k) for p in manager.profiles for k in [*METRICS,'measured_at']]
                       + [Pending(manager)])


class Metric(ListenerMixin, SensorEntity):
    def __init__(self, manager, person, key):
        self.manager, self.person, self.key = manager, person, key
        self._attr_unique_id = f'{DOMAIN}_{person}_{key}'
        self.entity_id = f'sensor.scale_{person}_{key}'
        self._attr_name = manager.profiles[person]['name']+' '+('测量时间' if key=='measured_at' else METRICS[key][0])
        if key == 'measured_at':
            self._attr_device_class = SensorDeviceClass.TIMESTAMP
        else:
            _, self._attr_native_unit_of_measurement, self._attr_suggested_display_precision = METRICS[key]
            self._attr_state_class = SensorStateClass.MEASUREMENT

    @property
    def native_value(self):
        row = self.manager.state['latest'].get(self.person)
        if not row:
            return None
        if self.key == 'measured_at':
            return datetime.fromisoformat(row['measured_at'])
        return row['metrics'].get(self.key)

    @property
    def extra_state_attributes(self):
        row = self.manager.state['latest'].get(self.person)
        if row is None:
            return {'profile_id': self.person, 'archive_history': [],
                    'history_origin': 'corrected_measurement_archive', 'archive_time_zone': self.manager.time_zone}
        origin = ('scale_measurement' if self.key in ('weight','impedance','heart_rate') else
                  'ha_receipt_time' if self.key == 'measured_at' else
                  'weight_and_height' if self.key == 'bmi' else 'impedance_model_estimate')
        attributes = dict(profile_id=self.person, person_entity=self.manager.options['person_entities'].get(self.person),
                    measurement_id=row['id'], measured_at=row['measured_at'], assignment=row['reason'],
                    age_at_measurement=row['metrics']['age'], height_cm=row['metrics']['height_cm'],
                    activity_factor=row['metrics']['activity_factor'], model_available=row['metrics']['model_available'],
                    value_origin=origin, calculation_method='impedance_model', model_id=MODEL_ID,
                    model_commit=MODEL_COMMIT, model=MODEL_NAME, timestamp_origin='HA首次接收时间')
        if self.key in ('weight', 'body_fat'):
            attributes['archive_history'] = self.manager.state['history'][self.person]
            attributes['history_origin'] = 'corrected_measurement_archive'
            attributes['archive_time_zone'] = self.manager.time_zone
        return attributes


class Pending(ListenerMixin, SensorEntity):
    def __init__(self, manager):
        self.manager = manager
        self._attr_unique_id = DOMAIN+'_pending'
        self._attr_name = '称重 待认领数量'
        self.entity_id = 'sensor.scale_pending_count'

    @property
    def native_value(self):
        return self.manager.state['pending_count']

    @property
    def extra_state_attributes(self):
        return dict(records=[{**r['sample'], 'id':r['id'], 'measured_at':r['measured_at'], 'reason':r['reason']}
                             for r in self.manager.state['pending']], references=self.manager.state['references'])

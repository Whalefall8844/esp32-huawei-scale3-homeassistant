"""GPL-3.0-or-later. Port of openScale HuaweiScale3BodyComposition.
Source: oliexdev/openScale commit 8ef93b2092745ea84ddd677eac1871452c27b3d5.
Independent adult four-electrode model, not the Huawei SDK.
"""
from datetime import date
import math

MODEL_ID = 'openscale_scale3_adult_4e'
MODEL_COMMIT = '8ef93b2092745ea84ddd677eac1871452c27b3d5'
MODEL_NAME = 'openScale Scale3 adult four-electrode estimate'

COEFFICIENTS = {'male': {'waterKg': [0.446000315, 0.0140000556, 0.166999963, -0.103000253, -0.00104299708, 0.0356998392, 9.22059951], 'fatFreeMassKg': [0.114000146, -0.0149999678, 0.322000075, 0.272999903, -0.000979001428, 0.0170001265, -13.002915], 'muscleMassKg': [0.107999947, -0.0140000275, 0.305999991, 0.258999945, -0.000929995268, 0.016099534, -12.5056628], 'boneMineralKg': [-0.00400070678, -0.00200017483, 0.0160001373, 0.0260008239, -5.79943116e-05, 0.00269952368, -1.506613], 'bmr': [6.43629949, -0.0002299374, 10.9972143, 4.74191847, 0.0238549107, -6.20381101, -233.995162], 'visceralFat': [-0.168283428, -0.0119657591, 0.377569276, -0.138136532, 0.000252403405, 0.134562343, 17.043261]}, 'female': {'waterKg': [0.445999704, 0.0139999486, 0.167000103, -0.102999643, -0.00186499864, 0.17269979, 2.47318013], 'fatFreeMassKg': [-5.91396e-08, -0.0150000073, 0.198000056, 0.313999865, -0.00148397395, 0.12459744, -16.3761132], 'muscleMassKg': [-5.96289e-08, -0.0130000008, 0.180999914, 0.286000079, -0.00136797854, 0.11559745, -14.1328394], 'boneMineralKg': [-0.0040015835, -0.00200033477, 0.0160000363, 0.0260015858, -0.000119001796, 0.00930013136, -1.6794094], 'bmr': [1.7578417, -0.181837584, 9.65817197, 5.89515491, 0.00782921597, -2.89574283, -203.303636], 'visceralFat': [-0.00158317035, 0.00544003197, 0.314994178, -0.103662636, 0.000967108734, 0.00527136893, -1.33727631]}}

def calculate(profile, measured_date, weight, impedance, heart_rate=None, activity=1.2):
    """Calculate age on the sample's local calendar date, not the current date."""
    birth = date.fromisoformat(profile["birth_date"])
    age = measured_date.year - birth.year - ((measured_date.month, measured_date.day) < (birth.month, birth.day))
    h, w = float(profile["height_cm"]), float(weight)
    if not math.isfinite(w) or w <= 0:
        raise ValueError("weight must be a positive finite number")
    result = dict(weight=w, bmi=w/(h/100)**2, impedance=impedance, heart_rate=heart_rate,
                  age=age, height_cm=h, activity_factor=activity, model_available=False,
                  calculation_method='impedance_model', model_id=MODEL_ID, model_commit=MODEL_COMMIT)
    if impedance is None or age < 18:
        return result
    z = float(impedance)
    if not all(math.isfinite(v) for v in [h,w,z]) or not (120<=h<=230 and 20<=w<=250 and 100<=z<=1000):
        return result
    x = [h*h/z,z,w,h,age*age,age,1]
    c = COEFFICIENTS[profile["sex"]]
    v = {key:sum(a*b for a,b in zip(values,x)) for key,values in c.items()}
    fat=(1-v["fatFreeMassKg"]/w)*100
    water=v["waterKg"]/w*100
    muscle=.605*v["muscleMassKg"]-1.833
    protein=(v["fatFreeMassKg"]-v["waterKg"]-v["boneMineralKg"])/w*100
    bmr=int(v["bmr"]); visceral=max(1,int(v["visceralFat"]))
    if not (0<=fat<=75 and 10<=water<=85 and 0<=muscle<=w and 0<=v["boneMineralKg"]<=10
            and 0<=protein<=40 and 500<=bmr<=5000 and 1<=visceral<=60):
        return result
    result.update(body_fat=fat, water=water, skeletal_muscle=muscle/w*100,
                  skeletal_muscle_kg=muscle, bone_mineral=v["boneMineralKg"],
                  lean_body_mass=v["fatFreeMassKg"], protein=protein, bmr=bmr,
                  tdee=bmr*activity, visceral_fat=visceral, model_available=True)
    return result

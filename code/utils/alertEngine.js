// utils/alertEngine.js - 阈值预警判断（温度/湿度/CO2/土壤超标 → 微信通知）
// 各类型默认预警配置（阈值 + 冷却分钟数）
function defaultAlert(type) {
    switch (type) {
        case 'humiture':
            return { enabled: false, tempMax: 32, tempMin: 5, humMax: 70, cooldown: 10 };
        case 'co2':
            return { enabled: false, co2Max: 1000, cooldown: 10 };
        case 'soil':
            return { enabled: false, soilMin: 20, cooldown: 10 };
        default:
            return { enabled: false, cooldown: 10 };
    }
}

// 是否支持阈值预警
function supportAlert(type) {
    return type === 'humiture' || type === 'co2' || type === 'soil';
}

// 检查某条解析后的数据是否触发预警；未触发返回 null
function checkAlert(device, parsed) {
    if (!device || !device.alert || !device.alert.enabled) return null;
    if (!parsed || !parsed.ok || !parsed.data) return null;

    var alert = device.alert;
    var data = parsed.data;
    var now = Date.now();
    var cooldownMs = (alert.cooldown || 10) * 60 * 1000;
    // 冷却期内不再重复提醒
    if (device.lastAlertAt && (now - device.lastAlertAt) < cooldownMs) return null;

    var kind = '';
    var why = '';
    switch (device.type) {
        case 'humiture':
            if (typeof data.temperature === 'number' && alert.tempMax != null && data.temperature > alert.tempMax) {
                kind = '温度过高';
                why = '温度 ' + data.temperature.toFixed(1) + '°C 超过上限 ' + alert.tempMax + '°C';
            } else if (typeof data.temperature === 'number' && alert.tempMin != null && data.temperature < alert.tempMin) {
                kind = '温度过低';
                why = '温度 ' + data.temperature.toFixed(1) + '°C 低于下限 ' + alert.tempMin + '°C';
            } else if (typeof data.humidity === 'number' && alert.humMax != null && data.humidity > alert.humMax) {
                kind = '湿度过高';
                why = '湿度 ' + data.humidity.toFixed(1) + '% 超过上限 ' + alert.humMax + '%';
            }
            break;
        case 'co2':
            if (typeof data.ppm === 'number' && alert.co2Max != null && data.ppm > alert.co2Max) {
                kind = 'CO2浓度过高';
                why = 'CO2 浓度 ' + data.ppm + 'ppm 超过上限 ' + alert.co2Max + 'ppm';
            }
            break;
        case 'soil':
            if (typeof data.pct === 'number' && alert.soilMin != null && data.pct < alert.soilMin) {
                kind = '土壤湿度过低';
                why = '土壤湿度 ' + data.pct.toFixed(1) + '% 低于下限 ' + alert.soilMin + '%';
            }
            break;
    }
    if (!why) return null;

    var name = device.name || device.topic;
    return {
        kind: kind,
        title: name,
        message: '【' + name + '】' + kind + '：' + why
    };
}

module.exports = {
    defaultAlert: defaultAlert,
    supportAlert: supportAlert,
    checkAlert: checkAlert
};

// utils/deviceParser.js - 设备消息解析
const TYPES = {
    LIGHT: 'light', // 灯控开关：on / off
    HUMITURE: 'humiture', // 温湿度：#28.0#57.6 或 28.0,57.6
    SOIL: 'soil', // 土壤湿度：0-1023 或 0-100%
    CO2: 'co2', // CO2 浓度：ppm 数值
    PHOTORES: 'photores', // 光敏：光照数值
    GENERIC: 'generic' // 通用传感器（兜底）
};

const TYPE_LIST = [TYPES.LIGHT, TYPES.HUMITURE, TYPES.SOIL, TYPES.CO2, TYPES.PHOTORES, TYPES.GENERIC];

const TYPE_LABELS = {
    light: '灯控开关',
    humiture: '温湿度',
    soil: '土壤湿度',
    co2: 'CO2浓度',
    photores: '光敏电阻',
    generic: '通用传感器'
};

const TYPE_ICONS = {
    light: '💡',
    humiture: '🌡️',
    soil: '🌱',
    co2: '🍃',
    photores: '☀️',
    generic: '📟'
};

function getTypeLabel(type) {
    return TYPE_LABELS[type] || '通用传感器';
}

function getTypeIcon(type) {
    return TYPE_ICONS[type] || '📟';
}

// ---------- 各类型解析 ----------
function parseLight(msg) {
    const m = String(msg == null ? '' : msg).trim().toLowerCase();
    if (m === 'on' || m === 'open' || m === '1' || m === 'true') {
        return { ok: true, data: { value: 'on', on: true, stateText: '开' } };
    }
    if (m === 'off' || m === 'close' || m === '0' || m === 'false') {
        return { ok: true, data: { value: 'off', on: false, stateText: '关' } };
    }
    return { ok: false, data: null, error: '无法识别的灯状态: ' + m };
}

function parseHumiture(msg) {
    const m = String(msg == null ? '' : msg).trim();
    let parts = null;
    if (m.indexOf('#') >= 0) {
        parts = m.split('#').filter((s) => s.trim() !== '');
    } else if (m.indexOf(',') >= 0) {
        parts = m.split(',');
    } else {
        parts = m.split(/\s+/);
    }
    if (!parts || parts.length < 2) {
        return { ok: false, data: null, error: '温湿度格式错误: ' + m };
    }
    const temperature = parseFloat(parts[0]);
    const humidity = parseFloat(parts[1]);
    if (isNaN(temperature) || isNaN(humidity)) {
        return { ok: false, data: null, error: '温湿度数值无效: ' + m };
    }
    return {
        ok: true,
        data: {
            temperature: temperature,
            humidity: humidity,
            tempText: temperature.toFixed(1),
            humText: humidity.toFixed(1)
        }
    };
}

function parseSoil(msg) {
    const m = String(msg == null ? '' : msg).trim();
    const raw = parseFloat(m);
    if (isNaN(raw)) {
        return { ok: false, data: null, error: '土壤湿度数值无效: ' + m };
    }
    const isPercent = m.indexOf('%') >= 0;
    let pct;
    if (isPercent) {
        pct = raw;
    } else if (raw <= 100) {
        pct = raw; // 直接为百分比
    } else {
        pct = (raw / 1023) * 100; // 0-1023 ADC 原始值
    }
    pct = Math.max(0, Math.min(100, pct));
    return {
        ok: true,
        data: {
            raw: raw,
            pct: pct,
            pctText: pct.toFixed(1),
            isPercent: isPercent
        }
    };
}

function parseCo2(msg) {
    const value = parseFloat(String(msg == null ? '' : msg).trim());
    if (isNaN(value) || value < 0) {
        return { ok: false, data: null, error: 'CO2 数值无效: ' + msg };
    }
    const v = Math.round(value);
    return { ok: true, data: { value: v, ppm: v, ppmText: String(v) } };
}

function parsePhotores(msg) {
    const value = parseFloat(String(msg == null ? '' : msg).trim());
    if (isNaN(value)) {
        return { ok: false, data: null, error: '光照数值无效: ' + msg };
    }
    return { ok: true, data: { value: value, lux: value, luxText: String(Math.round(value)) } };
}

function parseGeneric(msg) {
    return { ok: true, data: { value: String(msg == null ? '' : msg) } };
}

// ---------- 统一入口 ----------
function parseMessage(type, msg) {
    if (msg === null || msg === undefined || msg === '') {
        return { ok: false, data: null, error: '空消息' };
    }
    switch (type) {
        case TYPES.LIGHT:
            return parseLight(msg);
        case TYPES.HUMITURE:
            return parseHumiture(msg);
        case TYPES.SOIL:
            return parseSoil(msg);
        case TYPES.CO2:
            return parseCo2(msg);
        case TYPES.PHOTORES:
            return parsePhotores(msg);
        default:
            return parseGeneric(msg);
    }
}

// 设备数据展示摘要（首页卡片用）
function summarize(device) {
    const type = device.type || 'generic';
    const data = device.data || {};
    switch (type) {
        case 'light':
            return data.on ? '开' : (data.on === false ? '关' : '暂无数据');
        case 'humiture':
            if (typeof data.temperature === 'number' && typeof data.humidity === 'number') {
                return data.temperature.toFixed(1) + '°C / ' + data.humidity.toFixed(1) + '%';
            }
            break;
        case 'soil':
            if (typeof data.pct === 'number') {
                return data.pct.toFixed(1) + '%';
            }
            break;
        case 'co2':
            if (typeof data.ppm === 'number') {
                return data.ppm + ' ppm';
            }
            break;
        case 'photores':
            if (typeof data.value === 'number') {
                return String(Math.round(data.value));
            }
            break;
        default:
            if (device.raw) return device.raw;
    }
    return '暂无数据';
}

// 传感器详情项（详情页用）
function buildSensorItems(device) {
    const type = device.type || 'generic';
    const data = device.data || {};
    switch (type) {
        case 'humiture':
            return [
                { label: '温度', value: typeof data.temperature === 'number' ? data.temperature.toFixed(1) + '°C' : '--' },
                { label: '湿度', value: typeof data.humidity === 'number' ? data.humidity.toFixed(1) + '%' : '--' }
            ];
        case 'soil':
            return [
                { label: '土壤湿度', value: typeof data.pct === 'number' ? data.pct.toFixed(1) + '%' : '--' },
                { label: '原始值', value: typeof data.raw === 'number' ? String(data.raw) : '--' }
            ];
        case 'co2':
            return [
                { label: 'CO2 浓度', value: typeof data.ppm === 'number' ? data.ppm + ' ppm' : '--' },
                { label: '建议', value: co2Suggestion(data.ppm) }
            ];
        case 'photores':
            return [
                { label: '光照强度', value: typeof data.value === 'number' ? String(Math.round(data.value)) : '--' }
            ];
        default:
            return [
                { label: '原始消息', value: device.raw || '暂无数据' }
            ];
    }
}

function co2Suggestion(ppm) {
    if (typeof ppm !== 'number') return '--';
    if (ppm < 400) return '浓度偏低';
    if (ppm < 600) return '良好';
    if (ppm < 1000) return '正常';
    if (ppm < 2000) return '需通风';
    return '严重超标';
}

// 在线判定：最近收到消息时间在超时阈值内
function isOnline(device, timeoutMs, now) {
    if (!device || !device.lastUpdate) return false;
    const t = now || Date.now();
    return (t - device.lastUpdate) < timeoutMs;
}

module.exports = {
    TYPES,
    TYPE_LIST,
    TYPE_LABELS,
    TYPE_ICONS,
    getTypeLabel,
    getTypeIcon,
    parseMessage,
    summarize,
    buildSensorItems,
    isOnline
};

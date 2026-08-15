// utils/storage.js - 本地存储封装（设备列表 / 用户设置 / 消息记录）
const DEVICES_KEY = 'bemfa_devices_v1';
const SETTINGS_KEY = 'bemfa_settings_v1';
const LOG_KEY = 'bemfa_message_log_v1';
const ALERT_LOG_KEY = 'bemfa_alert_log_v1';
const MAX_LOG = 200;
const MAX_ALERT_LOG = 50;

// 默认设置（默认私钥占位为用户自己的巴法云私钥）
const DEFAULT_SETTINGS = {
    clientId: 'f6ebbe67c542c01ed1d796349dfd9a85',
    server: 'bemfa.com',
    port: 9504,
    path: '/wss',
    keepAlive: 60,
    autoConnect: true,
    onlineTimeout: 120 // 在线判定超时（秒）
};

// ---------- 设置 ----------
function initDefaults() {
    const settings = wx.getStorageSync(SETTINGS_KEY);
    if (!settings || typeof settings !== 'object' || Array.isArray(settings)) {
        wx.setStorageSync(SETTINGS_KEY, Object.assign({}, DEFAULT_SETTINGS));
        return;
    }
    let changed = false;
    Object.keys(DEFAULT_SETTINGS).forEach((k) => {
        if (settings[k] === undefined) {
            settings[k] = DEFAULT_SETTINGS[k];
            changed = true;
        }
    });
    if (changed) {
        wx.setStorageSync(SETTINGS_KEY, settings);
    }
}

function getSettings() {
    const s = wx.getStorageSync(SETTINGS_KEY);
    const valid = s && typeof s === 'object' && !Array.isArray(s);
    return Object.assign({}, DEFAULT_SETTINGS, valid ? s : {});
}

function saveSettings(settings) {
    const merged = Object.assign(getSettings(), settings || {});
    wx.setStorageSync(SETTINGS_KEY, merged);
    return merged;
}

// ---------- 设备 ----------
function getDevices() {
    const list = wx.getStorageSync(DEVICES_KEY);
    return Array.isArray(list) ? list : [];
}

function saveDevices(list) {
    wx.setStorageSync(DEVICES_KEY, Array.isArray(list) ? list : []);
}

function getDevice(topic) {
    if (!topic) return null;
    const list = getDevices();
    for (let i = 0; i < list.length; i++) {
        if (list[i].topic === topic) return list[i];
    }
    return null;
}

function addDevice(device) {
    if (!device || !device.topic) return { ok: false, error: '缺少主题' };
    const list = getDevices();
    for (let i = 0; i < list.length; i++) {
        if (list[i].topic === device.topic) {
            return { ok: false, error: '该主题已存在' };
        }
    }
    list.unshift(device);
    saveDevices(list);
    return { ok: true };
}

function updateDevice(topic, patch) {
    const list = getDevices();
    const idx = list.findIndex((d) => d.topic === topic);
    if (idx < 0) return { ok: false, error: '设备不存在' };
    list[idx] = Object.assign({}, list[idx], patch || {});
    saveDevices(list);
    return { ok: true };
}

function removeDevice(topic) {
    const list = getDevices().filter((d) => d.topic !== topic);
    saveDevices(list);
    return { ok: true };
}

// ---------- 消息记录 ----------
function getMessageLog() {
    const log = wx.getStorageSync(LOG_KEY);
    return Array.isArray(log) ? log : [];
}

function appendMessageLog(entry) {
    if (!entry) return;
    let log = getMessageLog();
    log.push(entry);
    if (log.length > MAX_LOG) {
        log = log.slice(log.length - MAX_LOG);
    }
    wx.setStorageSync(LOG_KEY, log);
}

function clearMessageLog() {
    wx.setStorageSync(LOG_KEY, []);
}

// ---------- 预警记录 ----------
function getAlertLog() {
    const log = wx.getStorageSync(ALERT_LOG_KEY);
    return Array.isArray(log) ? log : [];
}

function appendAlertLog(entry) {
    if (!entry) return;
    let log = getAlertLog();
    log.push(entry);
    if (log.length > MAX_ALERT_LOG) {
        log = log.slice(log.length - MAX_ALERT_LOG);
    }
    wx.setStorageSync(ALERT_LOG_KEY, log);
}

function clearAlertLog() {
    wx.setStorageSync(ALERT_LOG_KEY, []);
}

// ---------- 清空 ----------
function clearAll() {
    try {
        wx.removeStorageSync(DEVICES_KEY);
        wx.removeStorageSync(SETTINGS_KEY);
        wx.removeStorageSync(LOG_KEY);
        wx.removeStorageSync(ALERT_LOG_KEY);
    } catch (e) {
        // 忽略
    }
}

module.exports = {
    DEFAULT_SETTINGS,
    initDefaults,
    getSettings,
    saveSettings,
    getDevices,
    saveDevices,
    getDevice,
    addDevice,
    updateDevice,
    removeDevice,
    getMessageLog,
    appendMessageLog,
    clearMessageLog,
    getAlertLog,
    appendAlertLog,
    clearAlertLog,
    clearAll
};

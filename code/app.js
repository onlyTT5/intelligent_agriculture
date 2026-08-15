// app.js - 巴法云物联网设备管理小程序入口
const mqttClient = require('./utils/mqttClient');
const storage = require('./utils/storage');
const deviceParser = require('./utils/deviceParser');
const alertEngine = require('./utils/alertEngine');
const wechatAlert = require('./utils/wechatAlert');

App({
    globalData: {
        mqttStatus: 'disconnected', // disconnected | connecting | connected
        connectError: '',
        pendingEditTopic: '' // 管理页预填充编辑目标
    },

    _deviceUpdateListeners: [],

    onLaunch() {
        // 初始化默认设置（含默认私钥占位）
        storage.initDefaults();

        // MQTT 客户端单例
        this.mqttClient = mqttClient.getInstance();

        // 监听连接状态变化
        this.mqttClient.onStatusChange((status, info) => {
            this.globalData.mqttStatus = status;
            this.globalData.connectError = (info && info.error) || '';
            // 连接成功时确保订阅所有设备
            if (status === 'connected') {
                this.subscribeAllDevices();
            }
        });

        // 注册所有设备的订阅
        this.subscribeAllDevices();

        // 自动连接
        const settings = storage.getSettings();
        if (settings.autoConnect !== false && settings.clientId) {
            this.connect();
        }
    },

    // ---------- 连接控制 ----------
    connect() {
        const settings = storage.getSettings();
        const url = 'wss://' + settings.server + ':' + settings.port + settings.path;
        return this.mqttClient.connect({
            clientId: settings.clientId,
            url: url,
            keepAlive: settings.keepAlive || 60,
            autoReconnect: true
        });
    },

    disconnect() {
        this.mqttClient.disconnect();
    },

    getMqttStatus() {
        return this.mqttClient.getStatus();
    },

    // ---------- 设备订阅 ----------
    subscribeAllDevices() {
        const devices = storage.getDevices();
        devices.forEach((dev) => {
            this.subscribeDevice(dev);
        });
    },

    subscribeDevice(dev) {
        if (!dev || !dev.topic) return;
        this.mqttClient.subscribe(dev.topic, 0, (msg) => {
            this.handleDeviceMessage(dev.topic, msg);
        });
    },

    // ---------- 控制指令 ----------
    // 巴法云约定：向「主题/set」推送表示向订阅该主题的所有设备推送指令
    publish(device, payload, qos) {
        if (!device || !device.topic) return false;
        return this.mqttClient.publish(device.topic + '/set', payload, { qos: qos || 0, retain: false });
    },

    // ---------- 消息处理 ----------
    handleDeviceMessage(topic, msg) {
        const devices = storage.getDevices();
        const idx = devices.findIndex((d) => d.topic === topic);
        if (idx >= 0) {
            const dev = devices[idx];
            const parsed = deviceParser.parseMessage(dev.type, msg);
            dev.raw = msg;
            dev.lastUpdate = Date.now();
            if (parsed.ok) {
                dev.data = parsed.data;
            }
            dev.online = true;
            devices[idx] = dev;
            storage.saveDevices(devices);
            storage.appendMessageLog({
                topic: topic,
                message: msg,
                time: Date.now(),
                type: dev.type
            });
            // 阈值预警：超标自动推送微信通知
            this.checkAndSendAlert(dev, parsed);
        }
        this._emitDeviceUpdate(topic);
    },

    // ---------- 微信预警通知 ----------
    checkAndSendAlert(device, parsed) {
        const info = alertEngine.checkAlert(device, parsed);
        if (!info) return;
        const settings = storage.getSettings();
        const uid = settings.clientId;
        if (!uid) return;
        // 先记录冷却时间，避免重复轰炸（即使发送失败也不立即重试）
        storage.updateDevice(device.topic, { lastAlertAt: Date.now() });
        const self = this;
        wechatAlert.sendAlert(uid, info.title, info.message)
            .then((res) => {
                const ok = wechatAlert.isSuccess(res);
                storage.appendAlertLog({
                    topic: device.topic,
                    message: info.message,
                    ok: ok,
                    time: Date.now(),
                    error: ok ? '' : JSON.stringify(res)
                });
                self._emitDeviceUpdate(device.topic);
            })
            .catch((err) => {
                storage.appendAlertLog({
                    topic: device.topic,
                    message: info.message,
                    ok: false,
                    time: Date.now(),
                    error: (err && err.errMsg) || '网络错误'
                });
                self._emitDeviceUpdate(device.topic);
            });
    },

    // 发送测试微信通知（验证绑定是否成功）
    sendTestWechatAlert(deviceName, callback) {
        const settings = storage.getSettings();
        const uid = settings.clientId;
        if (!uid) {
            if (callback) callback({ ok: false, error: '未配置私钥' });
            return;
        }
        const name = deviceName || '巴法云小程序';
        wechatAlert.sendAlert(uid, name, '这是一条测试消息：微信通知功能已开启')
            .then((res) => {
                const ok = wechatAlert.isSuccess(res);
                if (callback) callback({ ok: ok, error: ok ? '' : JSON.stringify(res) });
            })
            .catch((err) => {
                if (callback) callback({ ok: false, error: (err && err.errMsg) || '网络错误' });
            });
    },

    // ---------- 设备更新事件（页面监听） ----------
    onDeviceUpdate(listener) {
        if (this._deviceUpdateListeners.indexOf(listener) < 0) {
            this._deviceUpdateListeners.push(listener);
        }
    },

    offDeviceUpdate(listener) {
        const idx = this._deviceUpdateListeners.indexOf(listener);
        if (idx >= 0) {
            this._deviceUpdateListeners.splice(idx, 1);
        }
    },

    _emitDeviceUpdate(topic) {
        this._deviceUpdateListeners.forEach((fn) => {
            try {
                fn(topic);
            } catch (e) {
                // 忽略监听器异常
            }
        });
    }
});

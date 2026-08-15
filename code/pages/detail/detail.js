// pages/detail/detail.js - 设备详情页（控制 / 查看数据）
var app = getApp();
var storage = require('../../utils/storage');
var parser = require('../../utils/deviceParser');
var alertEngine = require('../../utils/alertEngine');
var dateUtil = require('../../utils/date');

Page({
    data: {
        topic: '',
        device: null,
        notFound: false,
        typeIcon: '',
        typeLabel: '',
        summary: '',
        sensorItems: [],
        lightOn: false,
        sending: false,
        command: '',
        lastUpdateText: '',
        log: [],
        supportAlert: false,
        alertForm: {
            enabled: false,
            tempMax: '32',
            tempMin: '5',
            humMax: '70',
            co2Max: '1000',
            soilMin: '20',
            cooldown: '10'
        },
        lastAlert: null,
        alertTesting: false
    },

    _updateListener: null,
    _statusListener: null,

    onLoad: function (options) {
        var topic = decodeURIComponent(options.topic || '');
        this.setData({ topic: topic });
        wx.setNavigationBarTitle({ title: topic });
    },

    onShow: function () {
        var self = this;

        this._updateListener = function (t) {
            if (t === self.data.topic) {
                self.refresh();
                self.refreshLog();
            }
        };
        app.onDeviceUpdate(this._updateListener);

        this._statusListener = function () {
            // 状态变化无需重绘详情，仅保持监听以维持事件注册
        };
        app.mqttClient.onStatusChange(this._statusListener);

        this.ensureSubscribe();
        this.refresh();
        this.refreshLog();
    },

    onHide: function () {
        this._cleanup();
    },

    onUnload: function () {
        this._cleanup();
    },

    _cleanup: function () {
        if (this._updateListener) {
            app.offDeviceUpdate(this._updateListener);
            this._updateListener = null;
        }
        if (this._statusListener) {
            app.mqttClient.offStatusChange(this._statusListener);
            this._statusListener = null;
        }
    },

    ensureSubscribe: function () {
        var device = storage.getDevice(this.data.topic);
        if (device) {
            app.subscribeDevice(device);
        }
    },

    refresh: function () {
        var device = storage.getDevice(this.data.topic);
        if (!device) {
            this.setData({ notFound: true, device: null });
            return;
        }
        var settings = storage.getSettings();
        var onlineTimeout = (settings.onlineTimeout || 120) * 1000;
        var online = parser.isOnline(device, onlineTimeout);
        // 预警表单回填
        var supportAlert = alertEngine.supportAlert(device.type);
        var alertForm = this._buildAlertForm(device);
        var lastAlert = this._getLastAlert(device.topic);
        this.setData({
            device: Object.assign({}, device, { _online: online }),
            notFound: false,
            typeIcon: parser.getTypeIcon(device.type),
            typeLabel: parser.getTypeLabel(device.type),
            summary: parser.summarize(device),
            sensorItems: parser.buildSensorItems(device),
            lightOn: device.type === 'light' && !!(device.data && device.data.on),
            lastUpdateText: device.lastUpdate ? dateUtil.formatDateTime(device.lastUpdate) : '暂无',
            supportAlert: supportAlert,
            alertForm: alertForm,
            lastAlert: lastAlert
        });
    },

    _buildAlertForm: function (device) {
        var def = alertEngine.defaultAlert(device.type);
        var cur = device.alert || {};
        return {
            enabled: !!cur.enabled,
            tempMax: String(cur.tempMax != null ? cur.tempMax : def.tempMax),
            tempMin: String(cur.tempMin != null ? cur.tempMin : def.tempMin),
            humMax: String(cur.humMax != null ? cur.humMax : def.humMax),
            co2Max: String(cur.co2Max != null ? cur.co2Max : def.co2Max),
            soilMin: String(cur.soilMin != null ? cur.soilMin : def.soilMin),
            cooldown: String(cur.cooldown != null ? cur.cooldown : def.cooldown)
        };
    },

    _getLastAlert: function (topic) {
        var logs = storage.getAlertLog();
        var last = null;
        for (var i = logs.length - 1; i >= 0; i--) {
            if (logs[i].topic === topic) {
                last = logs[i];
                break;
            }
        }
        if (last) {
            last.timeText = dateUtil.formatDateTime(last.time);
        }
        return last;
    },

    refreshLog: function () {
        var log = storage.getMessageLog()
            .filter(function (l) { return l.topic === this.data.topic; }.bind(this))
            .slice(-20)
            .reverse();
        for (var i = 0; i < log.length; i++) {
            log[i].timeText = dateUtil.formatDateTime(log[i].time);
        }
        this.setData({ log: log });
    },

    // ---------- 灯控 ----------
    onLightChange: function (e) {
        var val = e.detail.value;
        var device = this.data.device;
        if (!device || this.data.sending) return;
        this.setData({ sending: true, lightOn: val });
        var payload = val ? 'on' : 'off';
        var self = this;
        var ok = app.publish(device, payload, 0);
        setTimeout(function () {
            self.setData({ sending: false });
            if (ok) {
                wx.showToast({ title: '已发送' + (val ? '开' : '关'), icon: 'success' });
            } else {
                wx.showToast({ title: '未连接，指令发送失败', icon: 'none' });
            }
        }, 400);
    },

    // ---------- 自定义指令 ----------
    onCommandInput: function (e) {
        this.setData({ command: e.detail.value });
    },

    onSendCommand: function () {
        var cmd = (this.data.command || '').trim();
        var device = this.data.device;
        if (!device) return;
        if (!cmd) {
            wx.showToast({ title: '请输入指令内容', icon: 'none' });
            return;
        }
        var ok = app.publish(device, cmd, 0);
        wx.showToast({
            title: ok ? '指令已发送' : '未连接，发送失败',
            icon: ok ? 'success' : 'none'
        });
    },

    // ---------- 微信预警 ----------
    onAlertSwitch: function (e) {
        this.setData({ 'alertForm.enabled': e.detail.value });
    },

    onAlertInput: function (e) {
        var field = e.currentTarget.dataset.field;
        var patch = {};
        patch['alertForm.' + field] = e.detail.value;
        this.setData(patch);
    },

    onSaveAlert: function () {
        var device = this.data.device;
        if (!device) return;
        var f = this.data.alertForm;
        var alert = {
            enabled: !!f.enabled,
            cooldown: parseInt(f.cooldown, 10) || 10,
            tempMax: parseFloat(f.tempMax),
            tempMin: parseFloat(f.tempMin),
            humMax: parseFloat(f.humMax),
            co2Max: parseFloat(f.co2Max),
            soilMin: parseFloat(f.soilMin)
        };
        storage.updateDevice(device.topic, { alert: alert });
        this.refresh();
        wx.showToast({ title: '已保存', icon: 'success' });
    },

    onTestAlert: function () {
        var device = this.data.device;
        var self = this;
        if (!device || this.data.alertTesting) return;
        this.setData({ alertTesting: true });
        app.sendTestWechatAlert(device.name || device.topic, function (res) {
            self.setData({ alertTesting: false });
            if (res.ok) {
                wx.showToast({ title: '已发送，请查收微信', icon: 'success' });
            } else {
                wx.showModal({
                    title: '测试通知发送失败',
                    content: '错误：' + res.error + '\n\n请确认：\n1. 已在巴法云控制台绑定微信；\n2. 已在微信公众平台配置 apis.bemfa.com 为 request 合法域名。',
                    showCancel: false
                });
            }
        });
    },

    // ---------- 其他操作 ----------
    onEdit: function () {
        app.globalData.pendingEditTopic = this.data.topic;
        wx.switchTab({ url: '/pages/manage/manage' });
    },

    onRefresh: function () {
        this.ensureSubscribe();
        this.refresh();
        this.refreshLog();
        wx.showToast({ title: '已刷新', icon: 'success' });
    },

    goBack: function () {
        wx.navigateBack();
    }
});

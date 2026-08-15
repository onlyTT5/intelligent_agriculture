// pages/mine/mine.js - 我的：MQTT 连接配置 / 连接状态 / 清缓存
var app = getApp();
var storage = require('../../utils/storage');

var STATUS_TEXT = {
    connected: '已连接',
    connecting: '连接中…',
    disconnected: '未连接'
};

Page({
    data: {
        form: {
            clientId: '',
            server: 'bemfa.com',
            port: '9504',
            path: '/wss',
            keepAlive: '60',
            autoConnect: true,
            onlineTimeout: '120'
        },
        mqttStatus: 'disconnected',
        statusText: '未连接',
        connectError: '',
        urlPreview: '',
        deviceCount: 0,
        msgLogCount: 0,
        alertLogCount: 0,
        wechatTesting: false
    },

    _statusListener: null,

    onShow: function () {
        var self = this;

        // 同步自定义底部导航栏选中态
        if (typeof this.getTabBar === 'function' && this.getTabBar()) {
            this.getTabBar().setData({ selected: 2 });
        }

        this._statusListener = function (status) {
            self.setData({
                mqttStatus: status,
                statusText: STATUS_TEXT[status] || '未连接',
                connectError: app.globalData.connectError
            });
        };
        app.mqttClient.onStatusChange(this._statusListener);

        this.setData({
            mqttStatus: app.getMqttStatus(),
            statusText: STATUS_TEXT[app.getMqttStatus()] || '未连接',
            connectError: app.globalData.connectError
        });

        this._loadForm();
        this._loadStats();
    },

    onHide: function () {
        if (this._statusListener) {
            app.mqttClient.offStatusChange(this._statusListener);
            this._statusListener = null;
        }
    },

    _loadForm: function () {
        var s = storage.getSettings();
        this.setData({
            form: {
                clientId: s.clientId,
                server: s.server,
                port: String(s.port),
                path: s.path,
                keepAlive: String(s.keepAlive),
                autoConnect: !!s.autoConnect,
                onlineTimeout: String(s.onlineTimeout || 120)
            }
        });
        this._updateUrlPreview();
    },

    _loadStats: function () {
        this.setData({
            deviceCount: storage.getDevices().length,
            msgLogCount: storage.getMessageLog().length,
            alertLogCount: storage.getAlertLog().length
        });
    },

    _updateUrlPreview: function () {
        var f = this.data.form;
        var port = f.port || '9504';
        var path = f.path || '/wss';
        var server = f.server || 'bemfa.com';
        this.setData({ urlPreview: 'wss://' + server + ':' + port + path });
    },

    onInput: function (e) {
        var field = e.currentTarget.dataset.field;
        var patch = {};
        patch['form.' + field] = e.detail.value;
        this.setData(patch);
        this._updateUrlPreview();
    },

    onSwitchChange: function (e) {
        this.setData({ 'form.autoConnect': e.detail.value });
    },

    onSave: function () {
        var f = this.data.form;
        var clientId = (f.clientId || '').trim();
        var server = (f.server || '').trim() || 'bemfa.com';
        var port = parseInt(f.port, 10) || 9504;
        var path = (f.path || '').trim() || '/wss';
        if (path.charAt(0) !== '/') path = '/' + path;
        var keepAlive = parseInt(f.keepAlive, 10) || 60;
        var onlineTimeout = parseInt(f.onlineTimeout, 10) || 120;
        if (!clientId) {
            wx.showToast({ title: '请填写私钥', icon: 'none' });
            return;
        }
        storage.saveSettings({
            clientId: clientId,
            server: server,
            port: port,
            path: path,
            keepAlive: keepAlive,
            autoConnect: !!f.autoConnect,
            onlineTimeout: onlineTimeout
        });
        this._loadForm();
        wx.showToast({ title: '已保存', icon: 'success' });
        // 若已连接，重连以应用新配置
        if (app.getMqttStatus() === 'connected') {
            app.connect();
        }
    },

    onConnect: function () {
        var clientId = (this.data.form.clientId || '').trim();
        if (!clientId) {
            wx.showToast({ title: '请先填写私钥', icon: 'none' });
            return;
        }
        app.connect();
    },

    onDisconnect: function () {
        app.disconnect();
    },

    // ---------- 微信通知 ----------
    onCopyBindUrl: function () {
        wx.setClipboardData({
            data: 'https://cloud.bemfa.com/tcp/wechat.html',
            success: function () {
                wx.showToast({ title: '绑定链接已复制', icon: 'success' });
            }
        });
    },

    onTestWechat: function () {
        var self = this;
        if (this.data.wechatTesting) return;
        this.setData({ wechatTesting: true });
        app.sendTestWechatAlert('巴法云小程序', function (res) {
            self.setData({ wechatTesting: false });
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

    onClearCache: function () {
        var self = this;
        wx.showModal({
            title: '清空本地缓存',
            content: '将删除设备列表、连接设置与消息记录。此操作不可恢复，确定继续？',
            confirmText: '清空',
            confirmColor: '#e64340',
            success: function (res) {
                if (!res.confirm) return;
                app.disconnect();
                app.mqttClient.clearSubscriptions();
                storage.clearAll();
                storage.initDefaults();
                self._loadForm();
                self._loadStats();
                self.setData({
                    mqttStatus: 'disconnected',
                    statusText: '未连接',
                    connectError: ''
                });
                wx.showToast({ title: '已清空', icon: 'success' });
            }
        });
    }
});

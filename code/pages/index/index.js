// pages/index/index.js - 首页：设备列表
var app = getApp();
var storage = require('../../utils/storage');
var parser = require('../../utils/deviceParser');
var dateUtil = require('../../utils/date');

var STATUS_TEXT = {
    connected: '已连接',
    connecting: '连接中…',
    disconnected: '未连接'
};

Page({
    data: {
        mqttStatus: 'disconnected',
        statusText: '未连接',
        connectError: '',
        devices: [],
        stats: { total: 0, online: 0, offline: 0 },
        empty: false
    },

    _updateListener: null,
    _statusListener: null,
    _onlineTimer: null,
    _onlineTimeout: 120000,

    onLoad: function () {
        var settings = storage.getSettings();
        this._onlineTimeout = (settings.onlineTimeout || 120) * 1000;
    },

    onShow: function () {
        var self = this;

        // 同步自定义底部导航栏选中态
        if (typeof this.getTabBar === 'function' && this.getTabBar()) {
            this.getTabBar().setData({ selected: 0 });
        }

        this._updateListener = function () {
            self.refresh();
        };
        app.onDeviceUpdate(this._updateListener);

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

        this.refresh();
        this._startOnlineTimer();
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
        if (this._onlineTimer) {
            clearInterval(this._onlineTimer);
            this._onlineTimer = null;
        }
    },

    _startOnlineTimer: function () {
        var self = this;
        if (this._onlineTimer) clearInterval(this._onlineTimer);
        this._onlineTimer = setInterval(function () {
            self._refreshOnline();
        }, 10000);
    },

    refresh: function () {
        var devices = storage.getDevices();
        var now = Date.now();
        for (var i = 0; i < devices.length; i++) {
            var d = devices[i];
            d._online = parser.isOnline(d, this._onlineTimeout, now);
            d._icon = parser.getTypeIcon(d.type);
            d._typeLabel = parser.getTypeLabel(d.type);
            d._summary = parser.summarize(d);
            d.lastUpdateText = d.lastUpdate ? dateUtil.formatRelative(d.lastUpdate) : '暂无数据';
            d.lastUpdateTime = d.lastUpdate ? dateUtil.formatDateTime(d.lastUpdate) : '';
        }
        var online = devices.filter(function (d) { return d._online; }).length;
        this.setData({
            devices: devices,
            stats: { total: devices.length, online: online, offline: devices.length - online },
            empty: devices.length === 0
        });
    },

    _refreshOnline: function () {
        var devices = this.data.devices;
        if (!devices.length) return;
        var now = Date.now();
        var changed = false;
        for (var i = 0; i < devices.length; i++) {
            var on = parser.isOnline(devices[i], this._onlineTimeout, now);
            if (on !== devices[i]._online) {
                devices[i]._online = on;
                changed = true;
            }
        }
        if (changed) {
            this.setData({
                devices: devices,
                stats: {
                    total: devices.length,
                    online: devices.filter(function (d) { return d._online; }).length,
                    offline: devices.filter(function (d) { return !d._online; }).length
                }
            });
        }
    },

    onPullDownRefresh: function () {
        this.refresh();
        wx.stopPullDownRefresh();
    },

    onTapDevice: function (e) {
        var topic = e.currentTarget.dataset.topic;
        wx.navigateTo({ url: '/pages/detail/detail?topic=' + encodeURIComponent(topic) });
    },

    onTapConnect: function () {
        app.connect();
    },

    goManage: function () {
        wx.switchTab({ url: '/pages/manage/manage' });
    }
});

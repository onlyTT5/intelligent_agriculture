// pages/manage/manage.js - 设备管理页（添加/删除/编辑 + 统计概览）
var app = getApp();
var storage = require('../../utils/storage');
var parser = require('../../utils/deviceParser');
var alertEngine = require('../../utils/alertEngine');
var dateUtil = require('../../utils/date');

var TOPIC_REG = /^[A-Za-z0-9_-]{1,32}$/;

Page({
    data: {
        stats: { total: 0, online: 0, offline: 0 },
        devices: [],
        empty: false,

        showForm: false,
        editingTopic: '',
        form: { topic: '', name: '', type: 'light', remark: '' },

        typeLabels: [],
        typeValues: [],
        typeIndex: 0
    },

    onLoad: function () {
        this.setData({
            typeLabels: parser.TYPE_LIST.map(function (t) { return parser.getTypeLabel(t); }),
            typeValues: parser.TYPE_LIST
        });
    },

    onShow: function () {
        // 同步自定义底部导航栏选中态
        if (typeof this.getTabBar === 'function' && this.getTabBar()) {
            this.getTabBar().setData({ selected: 1 });
        }

        // 详情页跳转编辑
        var pending = app.globalData.pendingEditTopic;
        if (pending) {
            app.globalData.pendingEditTopic = '';
            this.openEditForm(pending);
        }
        // 确保所有设备已订阅
        app.subscribeAllDevices();
        this.refresh();
    },

    refresh: function () {
        var devices = storage.getDevices();
        var settings = storage.getSettings();
        var onlineTimeout = (settings.onlineTimeout || 120) * 1000;
        var now = Date.now();
        for (var i = 0; i < devices.length; i++) {
            var d = devices[i];
            d._online = parser.isOnline(d, onlineTimeout, now);
            d._icon = parser.getTypeIcon(d.type);
            d._typeLabel = parser.getTypeLabel(d.type);
            d._summary = parser.summarize(d);
            d.createdText = d.createdAt ? dateUtil.formatDate(d.createdAt) : '';
        }
        var online = devices.filter(function (d) { return d._online; }).length;
        this.setData({
            devices: devices,
            stats: { total: devices.length, online: online, offline: devices.length - online },
            empty: devices.length === 0
        });
    },

    // ---------- 表单 ----------
    openAddForm: function () {
        this.setData({
            showForm: true,
            editingTopic: '',
            typeIndex: 0,
            form: { topic: '', name: '', type: 'light', remark: '' }
        });
    },

    openEditForm: function (topic) {
        var dev = storage.getDevice(topic);
        if (!dev) {
            this.refresh();
            return;
        }
        var typeIndex = this.data.typeValues.indexOf(dev.type);
        if (typeIndex < 0) typeIndex = 0;
        this.setData({
            showForm: true,
            editingTopic: dev.topic,
            typeIndex: typeIndex,
            form: {
                topic: dev.topic,
                name: dev.name || '',
                type: dev.type,
                remark: dev.remark || ''
            }
        });
    },

    closeForm: function () {
        this.setData({ showForm: false, editingTopic: '' });
    },

    onFormInput: function (e) {
        var field = e.currentTarget.dataset.field;
        var patch = {};
        patch['form.' + field] = e.detail.value;
        this.setData(patch);
    },

    onTypeChange: function (e) {
        var idx = Number(e.detail.value) || 0;
        this.setData({
            typeIndex: idx,
            'form.type': this.data.typeValues[idx] || 'light'
        });
    },

    onSave: function () {
        var f = this.data.form;
        var topic = (f.topic || '').trim();
        var name = (f.name || '').trim();
        var remark = (f.remark || '').trim();

        if (!topic) {
            wx.showToast({ title: '请填写主题', icon: 'none' });
            return;
        }
        if (!TOPIC_REG.test(topic)) {
            wx.showToast({ title: '主题仅支持字母/数字/下划线/中划线', icon: 'none' });
            return;
        }
        if (/\/set$|\/up$/.test(topic)) {
            wx.showToast({ title: '请勿携带 /set 或 /up 后缀', icon: 'none' });
            return;
        }

        var editing = this.data.editingTopic;
        if (editing) {
            // 校验新主题是否与其它设备冲突
            if (topic !== editing && storage.getDevice(topic)) {
                wx.showToast({ title: '该主题已存在', icon: 'none' });
                return;
            }
            var oldDev = storage.getDevice(editing);
            var patch = {
                topic: topic,
                name: name || topic,
                type: f.type,
                remark: remark
            };
            if (oldDev && oldDev.type !== f.type) {
                patch.alert = alertEngine.defaultAlert(f.type); // 类型变化时重置预警配置
            }
            var res = storage.updateDevice(editing, patch);
            if (!res.ok) {
                wx.showToast({ title: res.error, icon: 'none' });
                return;
            }
            // 主题变化时重新订阅
            app.mqttClient.unsubscribe(editing);
            app.subscribeDevice({ topic: topic, type: f.type });
        } else {
            if (storage.getDevice(topic)) {
                wx.showToast({ title: '该主题已存在', icon: 'none' });
                return;
            }
            var device = {
                topic: topic,
                name: name || topic,
                type: f.type,
                remark: remark,
                createdAt: Date.now(),
                raw: '',
                lastUpdate: 0,
                data: null,
                alert: alertEngine.defaultAlert(f.type)
            };
            var res2 = storage.addDevice(device);
            if (!res2.ok) {
                wx.showToast({ title: res2.error, icon: 'none' });
                return;
            }
            app.subscribeDevice(device);
        }

        this.setData({ showForm: false, editingTopic: '' });
        this.refresh();
        wx.showToast({ title: editing ? '已保存' : '已添加', icon: 'success' });
    },

    onEdit: function (e) {
        var topic = e.currentTarget.dataset.topic;
        this.openEditForm(topic);
    },

    onDelete: function (e) {
        var topic = e.currentTarget.dataset.topic;
        var self = this;
        wx.showModal({
            title: '删除设备',
            content: '确定删除设备「' + topic + '」？将同时取消该主题订阅。',
            confirmText: '删除',
            confirmColor: '#e64340',
            success: function (res) {
                if (res.confirm) {
                    storage.removeDevice(topic);
                    app.mqttClient.unsubscribe(topic);
                    if (self.data.editingTopic === topic) {
                        self.setData({ showForm: false, editingTopic: '' });
                    }
                    self.refresh();
                    wx.showToast({ title: '已删除', icon: 'success' });
                }
            }
        });
    },

    goDetail: function (e) {
        var topic = e.currentTarget.dataset.topic;
        wx.navigateTo({ url: '/pages/detail/detail?topic=' + encodeURIComponent(topic) });
    },

    noop: function () {
        // 阻止表单卡片点击冒泡
    }
});

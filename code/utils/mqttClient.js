// utils/mqttClient.js
// MQTT 3.1.1 协议实现（通过 wx.connectSocket WebSocket 二进制通道传输）
// 不依赖第三方 mqtt.js，纯手写协议编解码。
// 注意：巴法云不支持 QoS2（会被强制下线），本实现仅使用 QoS 0 / QoS 1。

// ---------- 控制报文类型 ----------
var PACKET = {
    CONNECT: 0x10,
    CONNACK: 0x20,
    PUBLISH: 0x30,
    PUBACK: 0x40,
    PUBREC: 0x50,
    PUBREL: 0x60,
    PUBCOMP: 0x70,
    SUBSCRIBE: 0x80,
    SUBACK: 0x90,
    UNSUBSCRIBE: 0xa0,
    UNSUBACK: 0xb0,
    PINGREQ: 0xc0,
    PINGRESP: 0xd0,
    DISCONNECT: 0xe0
};

var QOS = { AT_MOST_ONCE: 0, AT_LEAST_ONCE: 1 };

// ---------- UTF-8 编解码（小程序无 TextEncoder，用 encodeURIComponent 技巧） ----------
function utf8Encode(str) {
    str = String(str == null ? '' : str);
    var encoded = encodeURIComponent(str);
    var bytes = [];
    for (var i = 0; i < encoded.length; i++) {
        var ch = encoded.charAt(i);
        if (ch === '%') {
            bytes.push(parseInt(encoded.substr(i + 1, 2), 16));
            i += 2;
        } else {
            bytes.push(ch.charCodeAt(0));
        }
    }
    return bytes;
}

function utf8Decode(bytes) {
    var encoded = '';
    for (var i = 0; i < bytes.length; i++) {
        var b = bytes[i] & 0xff;
        if (b < 0x80) {
            encoded += String.fromCharCode(b);
        } else {
            var hex = b.toString(16);
            if (hex.length < 2) hex = '0' + hex;
            encoded += '%' + hex;
        }
    }
    try {
        return decodeURIComponent(encoded);
    } catch (e) {
        var out = '';
        for (var j = 0; j < bytes.length; j++) {
            out += String.fromCharCode(bytes[j] & 0xff);
        }
        return out;
    }
}

// UTF-8 字符串（两字节长度前缀）
function encodeUTF8String(str) {
    var bytes = utf8Encode(str);
    return [(bytes.length >> 8) & 0xff, bytes.length & 0xff].concat(bytes);
}

// ---------- 剩余长度编码（1-4 字节，每字节低 7 位） ----------
function encodeRemainingLength(length) {
    var out = [];
    do {
        var digit = length % 128;
        length = Math.floor(length / 128);
        if (length > 0) digit |= 0x80;
        out.push(digit);
    } while (length > 0);
    return out;
}

// 返回 { value, index }，index 指向剩余长度字段之后
function decodeRemainingLength(bytes, start) {
    var multiplier = 1;
    var value = 0;
    var index = start;
    var guard = 0;
    while (index < bytes.length && guard < 4) {
        var digit = bytes[index];
        value += (digit & 127) * multiplier;
        index++;
        guard++;
        if ((digit & 128) === 0) break;
        multiplier *= 128;
    }
    return { value: value, index: index };
}

// ---------- 报文构造 ----------
// CONNECT（Clean Session=1，私钥作为 clientId，无用户名密码）
function buildConnect(clientId, keepAlive) {
    var protocolName = [0x00, 0x04, 0x4d, 0x51, 0x54, 0x54]; // "MQTT"
    var protocolLevel = 0x04; // 3.1.1
    var connectFlags = 0x02; // Clean Session = 1
    var ka = keepAlive || 60;
    var keepAliveBytes = [(ka >> 8) & 0xff, ka & 0xff];
    var payload = encodeUTF8String(clientId || '');
    var body = protocolName.concat([protocolLevel, connectFlags], keepAliveBytes, payload);
    var remaining = encodeRemainingLength(body.length);
    return new Uint8Array([PACKET.CONNECT].concat(remaining, body)).buffer;
}

function buildSubscribe(packetId, topic, qos) {
    // SUBSCRIBE 固定头必须带标志位 0x02（0x80 | 0x02 = 0x82）
    var body = [(packetId >> 8) & 0xff, packetId & 0xff].concat(encodeUTF8String(topic), [qos & 0x03]);
    var remaining = encodeRemainingLength(body.length);
    return new Uint8Array([PACKET.SUBSCRIBE | 0x02].concat(remaining, body)).buffer;
}

function buildUnsubscribe(packetId, topic) {
    // UNSUBSCRIBE 固定头必须带标志位 0x02（0xA0 | 0x02 = 0xA2）
    var body = [(packetId >> 8) & 0xff, packetId & 0xff].concat(encodeUTF8String(topic));
    var remaining = encodeRemainingLength(body.length);
    return new Uint8Array([PACKET.UNSUBSCRIBE | 0x02].concat(remaining, body)).buffer;
}

function buildPublish(packetId, topic, payload, qos, retain) {
    qos = qos || 0;
    retain = !!retain;
    var header = PACKET.PUBLISH | ((qos & 0x03) << 1) | (retain ? 0x01 : 0x00);
    var body = encodeUTF8String(topic);
    if (qos > 0) {
        body.push((packetId >> 8) & 0xff, packetId & 0xff);
    }
    var payloadBytes = utf8Encode(payload);
    for (var i = 0; i < payloadBytes.length; i++) body.push(payloadBytes[i]);
    var remaining = encodeRemainingLength(body.length);
    return new Uint8Array([header].concat(remaining, body)).buffer;
}

function buildPingReq() {
    return new Uint8Array([PACKET.PINGREQ, 0x00]).buffer;
}

function buildDisconnect() {
    return new Uint8Array([PACKET.DISCONNECT, 0x00]).buffer;
}

// ---------- 报文解析 ----------
function parseConnack(bytes) {
    var sessionPresent = !!(bytes[2] & 0x01);
    var returnCode = bytes[3] || 0;
    return { sessionPresent: sessionPresent, returnCode: returnCode };
}

function parseSuback(bytes) {
    var packetId = ((bytes[2] & 0xff) << 8) | (bytes[3] & 0xff);
    var granted = bytes.slice(4);
    return { packetId: packetId, granted: granted };
}

function parsePublish(bytes) {
    var header = bytes[0];
    var qos = (header >> 1) & 0x03;
    var retain = header & 0x01;
    var dup = (header >> 3) & 0x01;
    var rem = decodeRemainingLength(bytes, 1);
    var idx = rem.index;
    var topicLen = ((bytes[idx] & 0xff) << 8) | (bytes[idx + 1] & 0xff);
    idx += 2;
    var topic = utf8Decode(bytes.slice(idx, idx + topicLen));
    idx += topicLen;
    var packetId = null;
    if (qos > 0) {
        packetId = ((bytes[idx] & 0xff) << 8) | (bytes[idx + 1] & 0xff);
        idx += 2;
    }
    var payload = utf8Decode(bytes.slice(idx));
    return { topic: topic, payload: payload, qos: qos, retain: retain, dup: dup, packetId: packetId };
}

// ---------- MQTT 客户端 ----------
var _instance = null;

function MqttClient() {
    this.socketTask = null;
    this.connected = false;
    this.connecting = false;
    this.manualClose = false;

    this.options = { clientId: '', url: '', keepAlive: 60, autoReconnect: true };

    this._connId = 0;
    this._statusListeners = [];
    this._globalMsgListeners = [];
    this._subs = {}; // topic -> { qos, callback }
    this._pendingAcks = {}; // packetId -> { type, topic }
    this._packetId = 0;

    this._keepAliveTimer = null;
    this._reconnectTimer = null;
    this._connackTimer = null;
    this._reconnectAttempts = 0;
    this._lastIncomingAt = 0;

    this._rxBuffer = []; // 接收缓冲（处理分包/粘包）
}

MqttClient.prototype.connect = function (options) {
    options = options || {};
    this.options.clientId = options.clientId || '';
    this.options.url = options.url || '';
    this.options.keepAlive = options.keepAlive || 60;
    this.options.autoReconnect = options.autoReconnect !== false;

    if (!this.options.url) {
        this._emitStatus('disconnected', { error: '未配置连接地址' });
        return false;
    }

    this.manualClose = false;
    this._clearTimers();
    this._teardownSocket();
    this.connecting = true;
    this.connected = false;
    this._emitStatus('connecting');

    this._connId += 1;
    var connId = this._connId;

    var self = this;
    try {
        this.socketTask = wx.connectSocket({
            url: this.options.url,
            timeout: 15000,
            binaryType: 'arraybuffer',
            fail: function (err) {
                if (self._connId !== connId) return;
                self.connecting = false;
                self._emitStatus('disconnected', { error: (err && err.errMsg) || '连接失败' });
                self._scheduleReconnect();
            }
        });
    } catch (e) {
        this.connecting = false;
        this._emitStatus('disconnected', { error: '连接异常' });
        return false;
    }

    this.socketTask.onOpen(function () {
        if (self._connId !== connId) return;
        self._handleOpen();
    });
    this.socketTask.onMessage(function (res) {
        if (self._connId !== connId) return;
        self._handleMessage(res);
    });
    this.socketTask.onError(function (err) {
        if (self._connId !== connId) return;
        self._handleError(err);
    });
    this.socketTask.onClose(function () {
        if (self._connId !== connId) return;
        self._handleClose();
    });
    return true;
};

MqttClient.prototype.disconnect = function () {
    this.manualClose = true;
    this._clearTimers();
    if (this.connected) {
        try {
            this._sendBuffer(buildDisconnect());
        } catch (e) {
            // 忽略
        }
    }
    this.connected = false;
    this.connecting = false;
    this._teardownSocket();
    this._emitStatus('disconnected');
};

// ---------- 连接事件 ----------
MqttClient.prototype._handleOpen = function () {
    this._lastIncomingAt = Date.now();
    this._sendBuffer(buildConnect(this.options.clientId, this.options.keepAlive));
    this._startKeepAlive();
    this._startConnackTimer();
};

// CONNACK 超时：连接打开后 15 秒内未收到 CONNACK 判定失败，触发重连
MqttClient.prototype._startConnackTimer = function () {
    var self = this;
    this._clearConnackTimer();
    this._connackTimer = setTimeout(function () {
        self._connackTimer = null;
        if (self.connected) return;
        self._emitStatus('disconnected', { error: '连接超时' });
        self._teardownSocket(); // 触发 onClose → 自动重连
    }, 15000);
};

MqttClient.prototype._clearConnackTimer = function () {
    if (this._connackTimer) {
        clearTimeout(this._connackTimer);
        this._connackTimer = null;
    }
};

MqttClient.prototype._handleMessage = function (res) {
    this._lastIncomingAt = Date.now();
    var data = res && res.data;
    if (!data) return;
    var u8 = data instanceof ArrayBuffer ? new Uint8Array(data) : new Uint8Array(data.buffer || data);
    this._feed(u8);
};

MqttClient.prototype._handleError = function (err) {
    this.connected = false;
    this.connecting = false;
    this._emitStatus('disconnected', { error: (err && err.errMsg) || '网络错误' });
};

MqttClient.prototype._handleClose = function () {
    this._clearKeepAlive();
    this._clearConnackTimer();
    this.connected = false;
    this.connecting = false;
    if (!this.manualClose) {
        this._emitStatus('disconnected', { error: '连接已断开' });
        this._scheduleReconnect();
    } else {
        this._emitStatus('disconnected');
    }
};

// ---------- 接收解析 ----------
MqttClient.prototype._feed = function (u8) {
    for (var i = 0; i < u8.length; i++) {
        this._rxBuffer.push(u8[i]);
    }
    for (; ;) {
        if (this._rxBuffer.length < 2) break;
        var rem = decodeRemainingLength(this._rxBuffer, 1);
        var totalLen = rem.index + rem.value;
        if (this._rxBuffer.length < totalLen) break; // 等待更多数据
        var packetBytes = this._rxBuffer.slice(0, totalLen);
        this._rxBuffer = this._rxBuffer.slice(totalLen);
        this._dispatch(packetBytes);
    }
};

MqttClient.prototype._dispatch = function (bytes) {
    var type = bytes[0] & 0xf0;
    switch (type) {
        case PACKET.CONNACK:
            this._handleConnack(parseConnack(bytes));
            break;
        case PACKET.PUBLISH:
            this._handlePublish(parsePublish(bytes));
            break;
        case PACKET.SUBACK:
            this._handleSuback(parseSuback(bytes));
            break;
        case PACKET.UNSUBACK:
            this._cleanPendingById(bytes);
            break;
        case PACKET.PUBACK:
        case PACKET.PUBREC:
        case PACKET.PUBCOMP:
            this._cleanPendingById(bytes);
            break;
        case PACKET.PINGRESP:
            // 心跳正常
            break;
        default:
            break;
    }
};

MqttClient.prototype._handleConnack = function (connack) {
    this._clearConnackTimer();
    if (connack.returnCode === 0) {
        this.connected = true;
        this.connecting = false;
        this._reconnectAttempts = 0;
        this._emitStatus('connected');
        this._resubscribeAll();
    } else {
        this.connected = false;
        this.connecting = false;
        this._emitStatus('disconnected', { error: '连接被拒绝，返回码 ' + connack.returnCode });
        this._teardownSocket();
        this._scheduleReconnect();
    }
};

MqttClient.prototype._handlePublish = function (pub) {
    if (pub.qos === 1 && pub.packetId != null) {
        // 回 PUBACK
        var ack = new Uint8Array([PACKET.PUBACK, 0x02, (pub.packetId >> 8) & 0xff, pub.packetId & 0xff]).buffer;
        this._sendBuffer(ack);
    }
    if (pub.qos === 2) {
        // 巴法云不支持 QoS2，忽略
        return;
    }
    // 分发到订阅回调 + 全局监听
    var sub = this._subs[pub.topic];
    if (sub && typeof sub.callback === 'function') {
        try {
            sub.callback(pub.payload, pub.topic);
        } catch (e) {
            // 忽略回调异常
        }
    }
    for (var i = 0; i < this._globalMsgListeners.length; i++) {
        try {
            this._globalMsgListeners[i](pub.payload, pub.topic, pub);
        } catch (e) {
            // 忽略
        }
    }
};

MqttClient.prototype._handleSuback = function (suback) {
    var pending = this._pendingAcks[suback.packetId];
    if (pending) {
        delete this._pendingAcks[suback.packetId];
    }
    var code = suback.granted && suback.granted.length ? suback.granted[0] : -1;
    if (code === 0x80 && pending) {
        // 订阅失败，移除该订阅
        delete this._subs[pending.topic];
    }
};

MqttClient.prototype._cleanPendingById = function (bytes) {
    var pid = ((bytes[2] & 0xff) << 8) | (bytes[3] & 0xff);
    if (this._pendingAcks[pid]) {
        delete this._pendingAcks[pid];
    }
};

// ---------- 对外 API ----------
MqttClient.prototype.subscribe = function (topic, qos, callback) {
    if (!topic) return;
    this._subs[topic] = { qos: qos || 0, callback: callback || null };
    if (this.connected) {
        var id = this._nextPacketId();
        this._pendingAcks[id] = { type: 'sub', topic: topic };
        this._sendBuffer(buildSubscribe(id, topic, qos || 0));
    }
};

MqttClient.prototype.unsubscribe = function (topic) {
    if (!topic) return;
    if (this._subs[topic]) delete this._subs[topic];
    if (this.connected) {
        var id = this._nextPacketId();
        this._pendingAcks[id] = { type: 'unsub', topic: topic };
        this._sendBuffer(buildUnsubscribe(id, topic));
    }
};

MqttClient.prototype.publish = function (topic, payload, opts) {
    if (!topic) return false;
    if (!this.connected) return false;
    var qos = (opts && opts.qos) || 0;
    var retain = !!(opts && opts.retain);
    var id = 0;
    if (qos > 0) {
        id = this._nextPacketId();
        this._pendingAcks[id] = { type: 'pub', topic: topic };
    }
    this._sendBuffer(buildPublish(id, topic, payload, qos, retain));
    return true;
};

MqttClient.prototype.getStatus = function () {
    if (this.connected) return 'connected';
    if (this.connecting) return 'connecting';
    return 'disconnected';
};

MqttClient.prototype.isConnected = function () {
    return this.connected;
};

MqttClient.prototype.onStatusChange = function (fn) {
    if (this._statusListeners.indexOf(fn) < 0) this._statusListeners.push(fn);
};

MqttClient.prototype.offStatusChange = function (fn) {
    var idx = this._statusListeners.indexOf(fn);
    if (idx >= 0) this._statusListeners.splice(idx, 1);
};

MqttClient.prototype.onMessage = function (fn) {
    if (this._globalMsgListeners.indexOf(fn) < 0) this._globalMsgListeners.push(fn);
};

// ---------- 内部工具 ----------
MqttClient.prototype._nextPacketId = function () {
    this._packetId += 1;
    if (this._packetId > 0xffff) this._packetId = 1;
    return this._packetId;
};

MqttClient.prototype._sendBuffer = function (buffer) {
    if (!this.socketTask) return;
    try {
        this.socketTask.send({ data: buffer, fail: function () { } });
    } catch (e) {
        // 忽略
    }
};

MqttClient.prototype._resubscribeAll = function () {
    var self = this;
    var topics = Object.keys(this._subs);
    topics.forEach(function (topic) {
        if (!self.connected) return;
        var sub = self._subs[topic];
        var id = self._nextPacketId();
        self._pendingAcks[id] = { type: 'sub', topic: topic };
        self._sendBuffer(buildSubscribe(id, topic, sub.qos || 0));
    });
};

MqttClient.prototype._startKeepAlive = function () {
    var self = this;
    this._clearKeepAlive();
    var interval = Math.max(10000, (this.options.keepAlive || 60) * 1000);
    this._keepAliveTimer = setInterval(function () {
        if (!self.connected) return;
        // 长时间未收到任何数据判定断线
        if (Date.now() - self._lastIncomingAt > (self.options.keepAlive || 60) * 2500) {
            self._emitStatus('disconnected', { error: '心跳超时' });
            self._teardownSocket(); // 触发 onClose → 自动重连
            return;
        }
        self._sendBuffer(buildPingReq());
    }, interval);
};

MqttClient.prototype._clearKeepAlive = function () {
    if (this._keepAliveTimer) {
        clearInterval(this._keepAliveTimer);
        this._keepAliveTimer = null;
    }
};

MqttClient.prototype._clearTimers = function () {
    this._clearKeepAlive();
    this._clearConnackTimer();
    if (this._reconnectTimer) {
        clearTimeout(this._reconnectTimer);
        this._reconnectTimer = null;
    }
};

// 清空全部订阅（清缓存等场景使用）
MqttClient.prototype.clearSubscriptions = function () {
    this._subs = {};
    this._pendingAcks = {};
};

MqttClient.prototype._teardownSocket = function () {
    if (this.socketTask) {
        try {
            this.socketTask.close({});
        } catch (e) {
            // 忽略
        }
        this.socketTask = null;
    }
};

MqttClient.prototype._scheduleReconnect = function () {
    if (!this.options.autoReconnect || this.manualClose) return;
    if (this._reconnectTimer) return;
    this._reconnectAttempts += 1;
    var delay = Math.min(30000, 2000 * this._reconnectAttempts);
    var self = this;
    this._reconnectTimer = setTimeout(function () {
        self._reconnectTimer = null;
        if (self.manualClose) return;
        self.connect(self.options);
    }, delay);
};

MqttClient.prototype._emitStatus = function (status, info) {
    for (var i = 0; i < this._statusListeners.length; i++) {
        try {
            this._statusListeners[i](status, info || {});
        } catch (e) {
            // 忽略
        }
    }
};

function getInstance() {
    if (!_instance) _instance = new MqttClient();
    return _instance;
}

module.exports = {
    getInstance: getInstance,
    MqttClient: MqttClient,
    PACKET: PACKET,
    QOS: QOS
};

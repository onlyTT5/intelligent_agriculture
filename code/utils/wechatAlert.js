// utils/wechatAlert.js - 巴法云微信消息推送 API 封装
// 文档：https://cloud.bemfa.com/docs/src/api_wechat.html
// 前提：需先在巴法云控制台绑定微信（https://cloud.bemfa.com/tcp/wechat.html）
var API_BASE = 'https://apis.bemfa.com/vb/wechat/v1';

function request(url, data) {
    return new Promise(function (resolve, reject) {
        wx.request({
            url: url,
            method: 'POST',
            header: { 'content-type': 'application/json; charset=utf-8' },
            data: data,
            success: function (res) {
                resolve(res.data || {});
            },
            fail: function (err) {
                reject(err);
            }
        });
    });
}

// 设备预警通知（微信模板消息）
// params: uid(用户私钥) device(设备名，自定义) message(消息内容)
function sendAlert(uid, device, message, opts) {
    opts = opts || {};
    var data = { uid: uid, device: device, message: message };
    if (opts.group) data.group = opts.group;
    if (opts.url) data.url = opts.url;
    return request(API_BASE + '/wechatAlertJson', data);
}

// 设备提醒（type 必须为 2）
function sendWarn(uid, device, message, opts) {
    opts = opts || {};
    var data = { uid: uid, type: 2, device: device, message: message };
    if (opts.group) data.group = opts.group;
    if (opts.url) data.url = opts.url;
    return request(API_BASE + '/wechatWarnJson', data);
}

// 判断接口返回是否成功：{"code":0,"msg":"success","data":{"code":0}}
function isSuccess(res) {
    return !!(res && (res.code === 0 || res.code === '0'));
}

module.exports = {
    sendAlert: sendAlert,
    sendWarn: sendWarn,
    isSuccess: isSuccess,
    API_BASE: API_BASE
};

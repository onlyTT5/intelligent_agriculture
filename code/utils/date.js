// utils/date.js - 日期格式化工具
function pad(n) {
    return n < 10 ? '0' + n : '' + n;
}

function formatTime(ts) {
    const d = ts ? new Date(ts) : new Date();
    return pad(d.getHours()) + ':' + pad(d.getMinutes()) + ':' + pad(d.getSeconds());
}

function formatDate(ts) {
    const d = ts ? new Date(ts) : new Date();
    return d.getFullYear() + '-' + pad(d.getMonth() + 1) + '-' + pad(d.getDate());
}

function formatDateTime(ts) {
    return formatDate(ts) + ' ' + formatTime(ts);
}

// 相对时间：刚刚 / N分钟前 / N小时前 / N天前 / 具体日期
function formatRelative(ts) {
    if (!ts) return '';
    const diff = Date.now() - ts;
    if (diff < 0) return '刚刚';
    const minute = 60 * 1000;
    const hour = 60 * minute;
    const day = 24 * hour;
    if (diff < minute) return '刚刚';
    if (diff < hour) return Math.floor(diff / minute) + ' 分钟前';
    if (diff < day) return Math.floor(diff / hour) + ' 小时前';
    if (diff < 7 * day) return Math.floor(diff / day) + ' 天前';
    return formatDate(ts);
}

module.exports = {
    pad,
    formatTime,
    formatDate,
    formatDateTime,
    formatRelative
};

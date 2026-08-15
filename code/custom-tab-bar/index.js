// custom-tab-bar/index.js - 自定义底部导航栏
Component({
    data: {
        selected: 0,
        list: [
            { pagePath: '/pages/index/index', text: '设备', icon: '🏠' },
            { pagePath: '/pages/manage/manage', text: '管理', icon: '🛠️' },
            { pagePath: '/pages/mine/mine', text: '我的', icon: '👤' }
        ]
    },

    lifetimes: {
        attached: function () {
            this._syncSelected();
        }
    },

    pageLifetimes: {
        show: function () {
            this._syncSelected();
        }
    },

    methods: {
        // 根据当前页面路由同步选中态
        _syncSelected: function () {
            var pages = getCurrentPages();
            var current = pages[pages.length - 1];
            var route = current && current.route;
            var list = this.data.list;
            var idx = 0;
            for (var i = 0; i < list.length; i++) {
                if (list[i].pagePath === '/' + route) {
                    idx = i;
                    break;
                }
            }
            if (idx !== this.data.selected) {
                this.setData({ selected: idx });
            }
        },

        onTabTap: function (e) {
            var index = Number(e.currentTarget.dataset.index);
            if (index === this.data.selected) return;
            var item = this.data.list[index];
            wx.switchTab({ url: item.pagePath });
        }
    }
});

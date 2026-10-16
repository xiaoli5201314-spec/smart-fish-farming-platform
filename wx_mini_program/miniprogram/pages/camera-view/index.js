const { isLoggedIn } = require('../../utils/auth')

Page({
  data: {
    src: ''
  },

  onLoad(options) {
    if (!isLoggedIn()) {
      wx.reLaunch({
        url: '/pages/auth/index'
      })
      return
    }

    const src = options && options.src ? decodeURIComponent(options.src) : ''
    this.setData({ src })
  }
})

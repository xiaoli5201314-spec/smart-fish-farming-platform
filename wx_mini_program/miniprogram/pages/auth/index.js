const {
  isLoggedIn,
  loginUser,
  registerUser
} = require('../../utils/auth')

Page({
  data: {
    mode: 'login',
    loading: false,
    username: '',
    password: '',
    confirmPassword: ''
  },

  onLoad() {
    this.redirectIfLoggedIn()
  },

  onShow() {
    this.redirectIfLoggedIn()
  },

  redirectIfLoggedIn() {
    if (!isLoggedIn()) return

    wx.switchTab({
      url: '/pages/index/index'
    })
  },

  switchMode(e) {
    const { mode } = e.currentTarget.dataset
    if (!mode || mode === this.data.mode) return

    this.setData({
      mode,
      password: '',
      confirmPassword: '',
      loading: false
    })
  },

  onInput(e) {
    const { field } = e.currentTarget.dataset
    if (!field) return

    this.setData({
      [field]: e.detail.value
    })
  },

  submit() {
    if (this.data.loading) return

    const username = this.data.username.trim()
    const password = this.data.password

    if (!username) {
      wx.showToast({
        title: '请输入账号',
        icon: 'none'
      })
      return
    }

    if (!password) {
      wx.showToast({
        title: '请输入密码',
        icon: 'none'
      })
      return
    }

    this.setData({
      loading: true
    })

    if (this.data.mode === 'register') {
      this.handleRegister(username, password)
      return
    }

    this.handleLogin(username, password)
  },

  handleLogin(username, password) {
    const result = loginUser(username, password)

    if (!result.ok) {
      this.setData({
        loading: false
      })
      wx.showToast({
        title: result.message,
        icon: 'none'
      })
      return
    }

    setTimeout(() => {
      wx.switchTab({
        url: '/pages/index/index'
      })
    }, 250)
  },

  handleRegister(username, password) {
    if (password !== this.data.confirmPassword) {
      this.setData({
        loading: false
      })
      wx.showToast({
        title: '两次密码不一致',
        icon: 'none'
      })
      return
    }

    const registerResult = registerUser(username, password)

    if (!registerResult.ok) {
      this.setData({
        loading: false
      })
      wx.showToast({
        title: registerResult.message,
        icon: 'none'
      })
      return
    }

    loginUser(username, password)
    setTimeout(() => {
      wx.switchTab({
        url: '/pages/index/index'
      })
    }, 250)
  }
})

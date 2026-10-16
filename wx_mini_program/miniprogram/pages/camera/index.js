const { isLoggedIn } = require('../../utils/auth')

const CAMERA_STORAGE_KEY = 'water_quality_camera_config'
const DEFAULT_CAMERA_CONFIG = {
  ipAddress: '192.0.2.10',
  port: '8080',
  controlPort: '8090'
}

function cleanHost(value) {
  return String(value || '')
    .replace(/^https?:\/\//i, '')
    .replace(/\/.*$/, '')
    .trim()
}

function cleanPort(value, fallback = DEFAULT_CAMERA_CONFIG.port) {
  const port = String(value || '').replace(/[^\d]/g, '')
  return port || fallback
}

function readCameraConfig() {
  try {
    const stored = wx.getStorageSync(CAMERA_STORAGE_KEY)
    if (stored && stored.ipAddress) {
      return {
        ipAddress: cleanHost(stored.ipAddress) || DEFAULT_CAMERA_CONFIG.ipAddress,
        port: cleanPort(stored.port, DEFAULT_CAMERA_CONFIG.port),
        controlPort: cleanPort(stored.controlPort, DEFAULT_CAMERA_CONFIG.controlPort)
      }
    }
  } catch (error) {}

  return { ...DEFAULT_CAMERA_CONFIG }
}

function makeCameraUrls(ipAddress, port) {
  const host = cleanHost(ipAddress) || DEFAULT_CAMERA_CONFIG.ipAddress
  const cleanCameraPort = cleanPort(port, DEFAULT_CAMERA_CONFIG.port)
  const baseUrl = `http://${host}:${cleanCameraPort}`

  return {
    baseUrl,
    streamUrl: `${baseUrl}/?action=stream`,
    homeUrl: baseUrl
  }
}

function makeControlUrl(ipAddress, controlPort, cmd) {
  const host = cleanHost(ipAddress) || DEFAULT_CAMERA_CONFIG.ipAddress
  const cleanControlPort = cleanPort(controlPort, DEFAULT_CAMERA_CONFIG.controlPort)
  return `http://${host}:${cleanControlPort}/control?cmd=${encodeURIComponent(cmd)}`
}

Page({
  data: {
    ipAddress: DEFAULT_CAMERA_CONFIG.ipAddress,
    port: DEFAULT_CAMERA_CONFIG.port,
    controlPort: DEFAULT_CAMERA_CONFIG.controlPort,
    baseUrl: '',
    streamUrl: '',
    homeUrl: '',
    entryState: '就绪',
    controlState: '远程就绪',
    motionState: '已停止',
    lightState: '照明待机',
    controlBusy: false,
    settingsOpen: false,
    pageEnter: false,
    cameraStats: [
      { label: '画面来源', value: '上位机摄像头' },
      { label: '视频服务', value: 'MJPG Streamer' },
      { label: '访问端口', value: '8080' }
    ]
  },

  onLoad() {
    if (!this.ensureLogin()) return
    this.loadCameraConfig()
  },

  onShow() {
    if (!this.ensureLogin()) return
    this.playPageEnter()
  },

  onHide() {
    this.clearPageEnterTimer()
  },

  onUnload() {
    this.clearPageEnterTimer()
  },

  playPageEnter() {
    this.clearPageEnterTimer()
    this.setData({ pageEnter: false })
    this.pageEnterTimer = setTimeout(() => {
      this.setData({ pageEnter: true })
    }, 20)
  },

  clearPageEnterTimer() {
    if (!this.pageEnterTimer) return

    clearTimeout(this.pageEnterTimer)
    this.pageEnterTimer = null
  },

  ensureLogin() {
    if (isLoggedIn()) {
      return true
    }

    wx.reLaunch({
      url: '/pages/auth/index'
    })
    return false
  },

  loadCameraConfig() {
    const config = readCameraConfig()
    const urls = makeCameraUrls(config.ipAddress, config.port)

    this.setData({
      ...config,
      ...urls,
      cameraStats: [
        { label: '画面来源', value: '上位机摄像头' },
        { label: '视频服务', value: 'MJPG Streamer' },
        { label: '访问端口', value: config.port }
      ]
    })
  },

  onInput(event) {
    const { field } = event.currentTarget.dataset
    if (!field) return

    this.setData({
      [field]: event.detail.value
    })
  },

  openSettings() {
    this.setData({ settingsOpen: true })
  },

  closeSettings() {
    this.setData({ settingsOpen: false })
  },

  noop() {},

  persistCameraConfig(showToast = true) {
    const ipAddress = cleanHost(this.data.ipAddress) || DEFAULT_CAMERA_CONFIG.ipAddress
    const port = cleanPort(this.data.port, DEFAULT_CAMERA_CONFIG.port)
    const controlPort = cleanPort(this.data.controlPort, DEFAULT_CAMERA_CONFIG.controlPort)
    const urls = makeCameraUrls(ipAddress, port)

    try {
      wx.setStorageSync(CAMERA_STORAGE_KEY, { ipAddress, port, controlPort })
    } catch (error) {}

    this.setData({
      ipAddress,
      port,
      controlPort,
      ...urls,
      entryState: '已保存',
      cameraStats: [
        { label: '画面来源', value: '上位机摄像头' },
        { label: '视频服务', value: 'MJPG Streamer' },
        { label: '访问端口', value: port }
      ]
    })

    if (showToast) {
      wx.showToast({
        title: '已保存',
        icon: 'success'
      })
    }

    return {
      ipAddress,
      port,
      controlPort,
      ...urls
    }
  },

  saveCameraConfig() {
    this.persistCameraConfig(true)
    this.setData({ settingsOpen: false })
  },

  openStream() {
    const urls = this.persistCameraConfig(false)
    wx.navigateTo({
      url: `/pages/camera-view/index?src=${encodeURIComponent(urls.streamUrl)}`
    })
  },

  onControlTap(event) {
    const { cmd, label } = event.currentTarget.dataset
    if (!cmd) return
    this.sendControlCommand(cmd, label || '控制指令')
  },

  sendControlCommand(cmd, label) {
    if (this.data.controlBusy) {
      return
    }

    const config = this.persistCameraConfig(false)
    const requestUrl = makeControlUrl(config.ipAddress, config.controlPort, cmd)

    this.setData({
      controlBusy: true,
      controlState: '发送中'
    })

    wx.request({
      url: requestUrl,
      method: 'GET',
      timeout: 3000,
      success: (res) => {
        const data = res.data || {}
        const ok = res.statusCode >= 200 && res.statusCode < 300 && data.ok !== false

        if (ok) {
          this.applyControlState(cmd, label)
          wx.showToast({
            title: '指令已发送',
            icon: 'success'
          })
        } else {
          this.setData({ controlState: '发送失败' })
          wx.showToast({
            title: data.message || '控制失败',
            icon: 'none'
          })
        }
      },
      fail: () => {
        this.setData({ controlState: '连接失败' })
        wx.showToast({
          title: '未连接上位机',
          icon: 'none'
        })
      },
      complete: () => {
        this.setData({ controlBusy: false })
      }
    })
  },

  applyControlState(cmd, label) {
    const updates = {
      controlState: label
    }

    if (cmd === 'front') {
      updates.motionState = '前进中'
    } else if (cmd === 'reverse') {
      updates.motionState = '后退中'
    } else if (cmd === 'stop') {
      updates.motionState = '已停止'
    } else if (cmd === 'light_on') {
      updates.lightState = '照明开启'
    } else if (cmd === 'light_off') {
      updates.lightState = '照明关闭'
    }

    this.setData(updates)
  },

  copyStreamUrl() {
    wx.setClipboardData({
      data: this.data.streamUrl,
      success: () => {
        wx.showToast({
          title: '地址已复制',
          icon: 'success'
        })
      }
    })
  }
})

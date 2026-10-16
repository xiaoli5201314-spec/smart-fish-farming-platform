const {
  getCurrentUser,
  isLoggedIn,
  logoutUser
} = require('../../utils/auth')
const waterQuality = require('../../utils/water-quality')

const EMPTY_HOME_WATER = waterQuality.buildIndexWaterState(waterQuality.makeEmptySnapshot())
const DEFAULT_WATER_API_URL = waterQuality.readWaterApiUrl()

const SHOWCASE_VIDEO_SRC = 'https://media.example.invalid/fish-farming-demo.mp4'
const WEATHER_STORAGE_KEY = 'water_quality_weather_city'
const WEATHER_REFRESH_MS = 5 * 60 * 1000
const WEATHER_CITIES = [
  { id: 'haikou_meilan', name: '海口美兰', shortName: '美兰', latitude: 20.0291, longitude: 110.3664 },
  { id: 'haikou_longhua', name: '海口龙华', shortName: '龙华', latitude: 20.0308, longitude: 110.3287 },
  { id: 'haikou_qiongshan', name: '海口琼山', shortName: '琼山', latitude: 19.9859, longitude: 110.3542 },
  { id: 'wenchang', name: '文昌', shortName: '文昌', latitude: 19.5433, longitude: 110.7977 },
  { id: 'qionghai', name: '琼海', shortName: '琼海', latitude: 19.2584, longitude: 110.4746 },
  { id: 'sanya', name: '三亚', shortName: '三亚', latitude: 18.2528, longitude: 109.5119 }
]
const DEFAULT_WEATHER_CITY = WEATHER_CITIES[0]

function formatWeatherTime(value) {
  if (!value) return '--:--'
  const text = String(value)
  const timePart = text.indexOf('T') >= 0 ? text.split('T')[1] : text.split(' ')[1]
  return timePart ? timePart.slice(0, 5) : text.slice(-5)
}

function formatNumber(value, digits = 0) {
  const number = Number(value)
  if (!Number.isFinite(number)) return '--'
  return number.toFixed(digits)
}

function makeWeatherUrl(city) {
  const current = [
    'temperature_2m',
    'relative_humidity_2m',
    'precipitation',
    'weather_code',
    'wind_speed_10m',
    'wind_direction_10m'
  ].join(',')
  const hourly = [
    'temperature_2m',
    'precipitation_probability',
    'weather_code'
  ].join(',')

  return `https://api.open-meteo.com/v1/forecast?latitude=${city.latitude}&longitude=${city.longitude}&current=${current}&hourly=${hourly}&forecast_days=1&timezone=Asia%2FShanghai`
}

function getWeatherCodeInfo(code) {
  const value = Number(code)
  if (value === 0) return { text: '晴', symbol: '晴', tone: 'sunny' }
  if ([1, 2].indexOf(value) >= 0) return { text: '少云', symbol: '云', tone: 'cloudy' }
  if (value === 3) return { text: '阴', symbol: '阴', tone: 'cloudy' }
  if ([45, 48].indexOf(value) >= 0) return { text: '雾', symbol: '雾', tone: 'mist' }
  if (value >= 51 && value <= 55) return { text: '毛毛雨', symbol: '雨', tone: 'rainy' }
  if (value >= 56 && value <= 67) return { text: '降雨', symbol: '雨', tone: 'rainy' }
  if (value >= 71 && value <= 77) return { text: '降雪', symbol: '雪', tone: 'rainy' }
  if (value >= 80 && value <= 82) return { text: '阵雨', symbol: '雨', tone: 'rainy' }
  if (value >= 95) return { text: '雷雨', symbol: '雷', tone: 'thunder' }
  return { text: '多云', symbol: '云', tone: 'cloudy' }
}

function getWindDirectionText(degree) {
  const value = Number(degree)
  if (!Number.isFinite(value)) return '--'
  const directions = ['北风', '东北风', '东风', '东南风', '南风', '西南风', '西风', '西北风']
  const index = Math.round(value / 45) % directions.length
  return directions[index]
}

function makeEmptyHourly() {
  return ['现在', '+1h', '+2h', '+3h'].map((time) => ({
    time,
    temp: '--',
    rain: '--',
    text: '--'
  }))
}

function makeWeatherStats(humidity, wind, rain) {
  return [
    { label: '湿度', value: humidity },
    { label: '风况', value: wind },
    { label: '降雨', value: rain }
  ]
}

function makeWeatherCityList(activeId) {
  return WEATHER_CITIES.map((city) => ({
    ...city,
    active: city.id === activeId
  }))
}

function makeWeatherState(city, overrides = {}) {
  return {
    cityId: city.id,
    city: city.name,
    shortName: city.shortName,
    temp: '--',
    text: '等待同步',
    symbol: '气',
    tone: 'idle',
    humidity: '--',
    wind: '--',
    rain: '--',
    update: '--',
    note: '5分钟自动刷新',
    loading: false,
    loaded: false,
    stats: makeWeatherStats('--', '--', '--'),
    hourly: makeEmptyHourly(),
    ...overrides
  }
}

function getStoredWeatherCity() {
  try {
    const storedId = wx.getStorageSync(WEATHER_STORAGE_KEY)
    return WEATHER_CITIES.find((city) => city.id === storedId) || DEFAULT_WEATHER_CITY
  } catch (error) {
    return DEFAULT_WEATHER_CITY
  }
}

function normalizeWeatherResponse(city, data) {
  if (!data || !data.current) {
    throw new Error('weather data empty')
  }

  const current = data.current
  const info = getWeatherCodeInfo(current.weather_code)
  const humidity = `${formatNumber(current.relative_humidity_2m, 0)}%`
  const windSpeed = `${formatNumber(current.wind_speed_10m, 1)}km/h`
  const wind = `${getWindDirectionText(current.wind_direction_10m)} ${windSpeed}`
  const rain = `${formatNumber(current.precipitation, 1)}mm`
  const hourly = data.hourly || {}
  const timeList = hourly.time || []
  const nowKey = current.time ? String(current.time).slice(0, 13) : ''
  let startIndex = timeList.findIndex((time) => String(time).slice(0, 13) >= nowKey)
  if (startIndex < 0) startIndex = 0

  const nextHourly = timeList.slice(startIndex, startIndex + 4).map((time, offset) => {
    const index = startIndex + offset
    const itemInfo = getWeatherCodeInfo((hourly.weather_code || [])[index])
    return {
      time: offset === 0 ? '现在' : formatWeatherTime(time),
      temp: `${formatNumber((hourly.temperature_2m || [])[index], 0)}°`,
      rain: `${formatNumber((hourly.precipitation_probability || [])[index], 0)}%`,
      text: itemInfo.text
    }
  })

  return makeWeatherState(city, {
    temp: formatNumber(current.temperature_2m, 1),
    text: info.text,
    symbol: info.symbol,
    tone: info.tone,
    humidity,
    wind,
    rain,
    update: formatWeatherTime(current.time),
    note: `更新 ${formatWeatherTime(current.time)}`,
    loading: false,
    loaded: true,
    stats: makeWeatherStats(humidity, wind, rain),
    hourly: nextHourly.length ? nextHourly : makeEmptyHourly()
  })
}

function makeSamplingFlow() {
  return [
    { name: '采集', state: '传感器读取', active: false },
    { name: '解析', state: '数据换算', active: false },
    { name: '评估', state: '风险判断', active: false },
    { name: '显示', state: '看板刷新', active: true }
  ]
}

function makeMicroStats() {
  return [
    { label: '数据源', value: '真实采集' },
    { label: '传感器', value: '5路' },
    { label: '采集', value: '轮询' },
    { label: '模式', value: '实时' }
  ]
}

Page({
  data: {
    username: '',
    pondName: '1号育成塘',
    connected: EMPTY_HOME_WATER.connected,
    lastUpdate: EMPTY_HOME_WATER.lastUpdate,
    waterScore: EMPTY_HOME_WATER.waterScore,
    riskLevel: EMPTY_HOME_WATER.riskLevel,
    deviceIp: EMPTY_HOME_WATER.deviceIp,
    metrics: EMPTY_HOME_WATER.metrics,
    focusMetrics: EMPTY_HOME_WATER.focusMetrics,
    nitrogenMetrics: EMPTY_HOME_WATER.nitrogenMetrics,
    conductivityMetrics: EMPTY_HOME_WATER.conductivityMetrics,
    organicMetrics: EMPTY_HOME_WATER.organicMetrics,
    compositionMetrics: EMPTY_HOME_WATER.compositionMetrics,
    qualityBands: EMPTY_HOME_WATER.qualityBands,
    samplingFlow: makeSamplingFlow(),
    microStats: makeMicroStats(),
    industrialStatus: EMPTY_HOME_WATER.industrialStatus,
    showcaseVideo: {
      src: SHOWCASE_VIDEO_SRC
    },
    weather: makeWeatherState(DEFAULT_WEATHER_CITY),
    weatherCities: makeWeatherCityList(DEFAULT_WEATHER_CITY.id),
    settingsOpen: false,
    waterApiUrl: DEFAULT_WATER_API_URL,
    waterConfigState: '未测试',
    pageEnter: false
  },

  onLoad() {
    if (!this.ensureLogin()) return
    this.refreshUser()
    this.syncWaterQuality()
  },

  onShow() {
    if (!this.ensureLogin()) return
    this.playPageEnter()
    this.refreshUser()
    this.startWaterTimer()
    this.syncWaterQuality()
  },

  onHide() {
    this.clearPageEnterTimer()
    this.stopWaterTimer()
  },

  onUnload() {
    this.clearPageEnterTimer()
    this.stopWaterTimer()
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

  refreshUser() {
    const currentUser = getCurrentUser()
    this.setData({
      username: currentUser ? currentUser.username : ''
    })
  },

  initWeather() {
    const city = getStoredWeatherCity()
    this.selectedWeatherCity = city
    this.setData({
      weather: makeWeatherState(city),
      weatherCities: makeWeatherCityList(city.id)
    })
    this.startWeatherTimer()
    this.fetchWeather(city)
  },

  startWeatherTimer() {
    if (this.weatherTimer) return

    this.weatherTimer = setInterval(() => {
      this.fetchWeather()
    }, WEATHER_REFRESH_MS)
  },

  stopWeatherTimer() {
    if (!this.weatherTimer) return

    clearInterval(this.weatherTimer)
    this.weatherTimer = null
  },

  refreshWeather() {
    this.fetchWeather(this.selectedWeatherCity || getStoredWeatherCity(), true)
  },

  selectWeatherCity(event) {
    const index = Number(event.currentTarget.dataset.index)
    const city = WEATHER_CITIES[index]
    if (!city) return

    this.selectedWeatherCity = city
    try {
      wx.setStorageSync(WEATHER_STORAGE_KEY, city.id)
    } catch (error) {}

    this.setData({
      weather: makeWeatherState(city, {
        loading: true,
        note: '同步中'
      }),
      weatherCities: makeWeatherCityList(city.id)
    })
    this.fetchWeather(city, true)
  },

  fetchWeather(city = this.selectedWeatherCity || DEFAULT_WEATHER_CITY, manual = false) {
    const requestId = (this.weatherRequestId || 0) + 1
    this.weatherRequestId = requestId
    this.selectedWeatherCity = city

    this.setData({
      weather: {
        ...this.data.weather,
        cityId: city.id,
        city: city.name,
        shortName: city.shortName,
        loading: true,
        note: manual ? '手动同步中' : '同步中'
      },
      weatherCities: makeWeatherCityList(city.id)
    })

    wx.request({
      url: makeWeatherUrl(city),
      method: 'GET',
      timeout: 8000,
      success: (response) => {
        if (this.weatherRequestId !== requestId) return

        if (response.statusCode !== 200) {
          this.handleWeatherError(manual)
          return
        }

        try {
          const weather = normalizeWeatherResponse(city, response.data)
          this.setData({ weather })
        } catch (error) {
          this.handleWeatherError(manual)
        }
      },
      fail: () => {
        if (this.weatherRequestId !== requestId) return
        this.handleWeatherError(manual)
      }
    })
  },

  handleWeatherError(manual = false) {
    const current = this.data.weather || makeWeatherState(this.selectedWeatherCity || DEFAULT_WEATHER_CITY)
    this.setData({
      weather: {
        ...current,
        loading: false,
        note: current.loaded ? '天气同步失败，保留上次数据' : '天气同步失败',
        text: current.loaded ? current.text : '网络异常',
        symbol: current.loaded ? current.symbol : '气',
        tone: current.loaded ? current.tone : 'idle'
      }
    })

    if (manual) {
      wx.showToast({
        title: '天气同步失败',
        icon: 'none'
      })
    }
  },

  startWaterTimer() {
    if (this.waterTimer) return

    this.waterTimer = setInterval(() => {
      this.syncWaterQuality()
    }, waterQuality.WATER_REFRESH_MS)
  },

  stopWaterTimer() {
    if (!this.waterTimer) return

    clearInterval(this.waterTimer)
    this.waterTimer = null
  },

  openSettings() {
    this.setData({
      settingsOpen: true,
      waterApiUrl: waterQuality.readWaterApiUrl()
    })
  },

  closeSettings() {
    this.setData({
      settingsOpen: false
    })
  },

  onWaterApiInput(event) {
    this.setData({
      waterApiUrl: event.detail.value
    })
  },

  saveWaterConfig(showToast = true) {
    const apiUrl = waterQuality.persistWaterApiUrl(this.data.waterApiUrl)

    this.setData({
      waterApiUrl: apiUrl,
      waterConfigState: '已保存'
    })

    if (showToast) {
      wx.showToast({
        title: '已保存',
        icon: 'success'
      })
    }

    this.syncWaterQuality(apiUrl)
    return apiUrl
  },

  testWaterConfig() {
    const apiUrl = this.saveWaterConfig(false)

    this.setData({
      waterConfigState: '测试中'
    })

    waterQuality.probeWaterApi({ apiUrl }).then((snapshot) => {
      this.setData({
        ...waterQuality.buildIndexWaterState(snapshot),
        waterConfigState: '连接正常'
      })
      wx.showToast({
        title: '连接正常',
        icon: 'success'
      })
    }).catch(() => {
      this.setData({
        waterConfigState: '连接失败'
      })
      wx.showToast({
        title: '连接失败',
        icon: 'none'
      })
    })
  },

  syncWaterQuality(apiUrl = this.data.waterApiUrl || waterQuality.readWaterApiUrl()) {
    waterQuality.getWaterQualitySnapshot({ apiUrl }).then((snapshot) => {
      this.setData(waterQuality.buildIndexWaterState(snapshot))
    })
  },

  logout() {
    this.stopWaterTimer()
    logoutUser()
    wx.showToast({
      title: '已退出登录',
      icon: 'none'
    })

    setTimeout(() => {
      wx.reLaunch({
        url: '/pages/auth/index'
      })
    }, 250)
  },

  noop() {}
})

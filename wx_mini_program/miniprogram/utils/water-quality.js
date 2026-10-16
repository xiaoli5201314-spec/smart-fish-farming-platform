const DEFAULT_WATER_API_URL = 'http://192.0.2.10/api/water/latest'
const WATER_REFRESH_MS = 5000
const WATER_REQUEST_TIMEOUT = 6000

const WATER_API_STORAGE_KEY = 'water_quality_data_api_config'
const WATER_SNAPSHOT_CACHE_KEY = 'water_quality_snapshot_cache'
const WATER_HISTORY_CACHE_KEY = 'water_quality_history_cache'
const HISTORY_LIMIT = 16

const METRIC_META = [
  { key: 'waterTemp', name: '水温', unit: '°C', tone: 'temp' },
  { key: 'ph', name: 'pH', unit: 'pH', tone: 'ph' },
  { key: 'conductivity', name: '电导率', unit: 'uS/cm', tone: 'ec' },
  { key: 'tds', name: 'TDS', unit: 'ppm', tone: 'tds' },
  { key: 'salinity', name: '盐度', unit: 'PPT', tone: 'salt' },
  { key: 'cod', name: 'COD', unit: 'mg/L', tone: 'cod' },
  { key: 'ammonia', name: '氨氮', unit: 'ppm', tone: 'nitrogen' },
  { key: 'nitrite', name: '亚硝酸根', unit: 'ppm', tone: 'nitrite' },
  { key: 'nitrate', name: '硝酸根', unit: 'ppm', tone: 'nitrate' }
]

const METRIC_RANGES = {
  waterTemp: [18, 35],
  ph: [6, 9],
  conductivity: [0, 2500],
  tds: [0, 1200],
  salinity: [0, 2],
  cod: [0, 50],
  ammonia: [0, 0.5],
  nitrite: [0, 0.2],
  nitrate: [0, 40]
}

const SENSOR_META = [
  { address: '01', name: 'COD传感器', metric: 'COD', key: 'cod', unit: 'mg/L', tone: 'amber' },
  { address: '02', name: 'PH-EC二合一', metric: 'pH/EC', key: 'ph', unit: 'pH', tone: 'green' },
  { address: '03', name: '氨氮电极', metric: 'NH4+', key: 'ammonia', unit: 'ppm', tone: 'cyan' },
  { address: '04', name: '亚硝酸根电极', metric: 'NO2-', key: 'nitrite', unit: 'ppm', tone: 'blue' },
  { address: '05', name: '硝酸根电极', metric: 'NO3-', key: 'nitrate', unit: 'ppm', tone: 'green' }
]

function clamp(value, min, max) {
  return Math.max(min, Math.min(max, value))
}

function pad(num) {
  return String(num).padStart(2, '0')
}

function formatTime(value) {
  if (!value) return '--:--:--'

  if (typeof value === 'number') {
    const date = new Date(value)
    return `${pad(date.getHours())}:${pad(date.getMinutes())}:${pad(date.getSeconds())}`
  }

  const text = String(value).trim()
  if (!text) return '--:--:--'

  if (text.indexOf('T') >= 0) {
    const timePart = text.split('T')[1] || ''
    return timePart.slice(0, 8) || '--:--:--'
  }

  if (text.indexOf(' ') >= 0) {
    const timePart = text.split(' ')[1] || ''
    return timePart.slice(0, 8) || '--:--:--'
  }

  if (/^\d{2}:\d{2}:\d{2}$/.test(text)) {
    return text
  }

  return text.slice(-8) || '--:--:--'
}

function toNumber(value) {
  const number = Number(value)
  return Number.isFinite(number) ? number : null
}

function formatNumber(value, digits) {
  const number = toNumber(value)
  if (!Number.isFinite(number)) return '--'
  return number.toFixed(digits)
}

function metricDigits(key, value) {
  const number = Math.abs(Number(value))
  if (!Number.isFinite(number)) return 0

  if (key === 'waterTemp') return 1
  if (key === 'ph') return 2
  if (key === 'conductivity' || key === 'tds') return 0
  if (key === 'salinity') return 2
  if (key === 'cod') return number >= 100 ? 1 : 2
  if (number >= 1000) return 1
  if (number >= 10) return 2
  return 3
}

function formatMetricValue(key, value) {
  return formatNumber(value, metricDigits(key, value))
}

function makeEmptyMetricValues() {
  return {
    waterTemp: null,
    ph: null,
    conductivity: null,
    tds: null,
    salinity: null,
    cod: null,
    ammonia: null,
    nitrite: null,
    nitrate: null
  }
}

function makeEmptySnapshot(overrides = {}) {
  return {
    connected: false,
    stale: false,
    deviceIp: '未连接',
    updateTime: '--:--:--',
    metricValues: makeEmptyMetricValues(),
    source: 'empty',
    ...overrides
  }
}

function normalizeWaterApiUrl(value) {
  let url = String(value || '').trim()
  if (!url) return DEFAULT_WATER_API_URL

  if (!/^https?:\/\//i.test(url)) {
    url = `http://${url}`
  }

  url = url.replace(/\s/g, '')
  if (/\/api\/water\/latest\/?$/i.test(url)) {
    return url.replace(/\/$/, '')
  }

  return `${url.replace(/\/+$/, '')}/api/water/latest`
}

function readWaterApiUrl() {
  try {
    const stored = wx.getStorageSync(WATER_API_STORAGE_KEY)
    if (stored && stored.apiUrl) {
      return normalizeWaterApiUrl(stored.apiUrl)
    }
  } catch (error) {}

  return DEFAULT_WATER_API_URL
}

function persistWaterApiUrl(value) {
  const apiUrl = normalizeWaterApiUrl(value)
  safeSetStorage(WATER_API_STORAGE_KEY, { apiUrl })
  return apiUrl
}

function getApiHost(apiUrl) {
  const url = normalizeWaterApiUrl(apiUrl)
  const matched = url.match(/^https?:\/\/([^/]+)/i)
  return matched ? matched[1] : '未连接'
}

function safeGetStorage(key) {
  try {
    return wx.getStorageSync(key)
  } catch (error) {
    return null
  }
}

function safeSetStorage(key, value) {
  try {
    wx.setStorageSync(key, value)
  } catch (error) {}
}

function metricPercent(key, rawValue) {
  const value = Number(rawValue)
  if (!Number.isFinite(value)) return 0
  const range = METRIC_RANGES[key] || [0, 100]
  const percent = ((value - range[0]) / (range[1] - range[0])) * 100
  return clamp(Math.round(percent), 4, 96)
}

function getMetricMeta(key) {
  return METRIC_META.find((item) => item.key === key) || null
}

function pickNumber() {
  for (let index = 0; index < arguments.length; index += 1) {
    const value = toNumber(arguments[index])
    if (Number.isFinite(value)) {
      return value
    }
  }
  return null
}

function normalizeAddress(value) {
  if (value === undefined || value === null) return ''
  return String(value).trim().padStart(2, '0')
}

function findSensorByAddress(sensorMap, address) {
  return sensorMap[normalizeAddress(address)] || null
}

function buildSensorMap(payload) {
  const source = Array.isArray(payload && payload.sensors) ? payload.sensors : []
  return source.reduce((map, item) => {
    const address = normalizeAddress(item.address || item.id || item.addr)
    if (address) {
      map[address] = item
    }
    return map
  }, {})
}

function sensorNumber(sensor, keys) {
  if (!sensor) return null

  for (let index = 0; index < keys.length; index += 1) {
    const key = keys[index]
    const value = pickNumber(sensor[key])
    if (Number.isFinite(value)) {
      return value
    }
  }

  return null
}

function normalizeMetricValues(payload) {
  const metricSource = payload && typeof payload.metrics === 'object' ? payload.metrics : {}
  const sensorMap = buildSensorMap(payload)

  const phEcSensor = findSensorByAddress(sensorMap, '02')
  const codSensor = findSensorByAddress(sensorMap, '01')
  const ammoniaSensor = findSensorByAddress(sensorMap, '03')
  const nitriteSensor = findSensorByAddress(sensorMap, '04')
  const nitrateSensor = findSensorByAddress(sensorMap, '05')

  const values = makeEmptyMetricValues()

  values.waterTemp = pickNumber(
    metricSource.waterTemp,
    metricSource.temperature,
    metricSource.temp,
    metricSource.phEcTemperature,
    sensorNumber(phEcSensor, ['temperatureC', 'temperature', 'tempC', 'temp'])
  )
  values.ph = pickNumber(
    metricSource.ph,
    metricSource.pH,
    sensorNumber(phEcSensor, ['ph', 'pH', 'value'])
  )
  values.conductivity = pickNumber(
    metricSource.conductivity,
    metricSource.ec,
    metricSource.ecUsCm,
    sensorNumber(phEcSensor, ['conductivity', 'ec', 'ecUsCm'])
  )
  values.cod = pickNumber(
    metricSource.cod,
    metricSource.codMgL,
    sensorNumber(codSensor, ['cod', 'codMgL', 'value'])
  )
  values.ammonia = pickNumber(
    metricSource.ammonia,
    metricSource.nh4,
    metricSource.ammoniaPpm,
    sensorNumber(ammoniaSensor, ['concentrationPpm', 'value', 'ammonia'])
  )
  values.nitrite = pickNumber(
    metricSource.nitrite,
    metricSource.no2,
    metricSource.nitritePpm,
    sensorNumber(nitriteSensor, ['concentrationPpm', 'value', 'nitrite'])
  )
  values.nitrate = pickNumber(
    metricSource.nitrate,
    metricSource.no3,
    metricSource.nitratePpm,
    sensorNumber(nitrateSensor, ['concentrationPpm', 'value', 'nitrate'])
  )

  values.tds = pickNumber(
    metricSource.tds,
    metricSource.tdsPpm,
    sensorNumber(phEcSensor, ['tds', 'tdsPpm'])
  )
  values.salinity = pickNumber(
    metricSource.salinity,
    metricSource.salinityPpt,
    sensorNumber(phEcSensor, ['salinity', 'salinityPpt'])
  )

  if (!Number.isFinite(values.tds) && Number.isFinite(values.conductivity)) {
    values.tds = Number((values.conductivity * 0.5).toFixed(0))
  }

  if (!Number.isFinite(values.salinity) && Number.isFinite(values.tds)) {
    values.salinity = Number((values.tds / 1000).toFixed(2))
  }

  return values
}

function hasAnyMetricValue(metricValues) {
  return METRIC_META.some((item) => Number.isFinite(toNumber(metricValues[item.key])))
}

function normalizeSnapshot(payload, apiUrl, options = {}) {
  const metricValues = normalizeMetricValues(payload || {})
  const connected = typeof payload.connected === 'boolean'
    ? payload.connected
    : hasAnyMetricValue(metricValues)

  return makeEmptySnapshot({
    connected,
    stale: Boolean(options.stale),
    deviceIp: String(payload.deviceIp || payload.ip || getApiHost(apiUrl)),
    updateTime: formatTime(payload.updateTime || payload.timestamp || payload.time || Date.now()),
    metricValues,
    source: options.source || 'remote'
  })
}

function getResponsePayload(body) {
  if (body && body.data && body.ok !== undefined && typeof body.data === 'object') {
    return body.data
  }

  if (body && body.snapshot && typeof body.snapshot === 'object') {
    return body.snapshot
  }

  return body
}

function appendHistory(snapshot) {
  if (!snapshot || !snapshot.connected || !hasAnyMetricValue(snapshot.metricValues)) {
    return
  }

  const history = readHistory()
  const latest = history[history.length - 1]
  const signature = JSON.stringify({
    updateTime: snapshot.updateTime,
    metricValues: snapshot.metricValues
  })

  if (latest && latest.signature === signature) {
    return
  }

  const nextHistory = history.concat([{
    signature,
    updateTime: snapshot.updateTime,
    metricValues: snapshot.metricValues
  }]).slice(-HISTORY_LIMIT)

  safeSetStorage(WATER_HISTORY_CACHE_KEY, nextHistory)
}

function readHistory() {
  const stored = safeGetStorage(WATER_HISTORY_CACHE_KEY)
  if (!Array.isArray(stored)) return []

  return stored
    .filter((item) => item && item.metricValues)
    .map((item) => ({
      signature: item.signature || '',
      updateTime: formatTime(item.updateTime),
      metricValues: {
        ...makeEmptyMetricValues(),
        ...item.metricValues
      }
    }))
}

function cacheSnapshot(snapshot) {
  if (!snapshot) return

  safeSetStorage(WATER_SNAPSHOT_CACHE_KEY, {
    connected: snapshot.connected,
    deviceIp: snapshot.deviceIp,
    updateTime: snapshot.updateTime,
    metricValues: snapshot.metricValues
  })
  appendHistory(snapshot)
}

function readCachedWaterSnapshot() {
  const stored = safeGetStorage(WATER_SNAPSHOT_CACHE_KEY)
  if (!stored || !stored.metricValues) {
    return makeEmptySnapshot()
  }

  return makeEmptySnapshot({
    connected: false,
    stale: true,
    deviceIp: stored.deviceIp || '未连接',
    updateTime: formatTime(stored.updateTime),
    metricValues: {
      ...makeEmptyMetricValues(),
      ...stored.metricValues
    },
    source: 'cache'
  })
}

function fetchRemoteSnapshot(apiUrl) {
  const url = normalizeWaterApiUrl(apiUrl)

  return new Promise((resolve, reject) => {
    wx.request({
      url,
      method: 'GET',
      timeout: WATER_REQUEST_TIMEOUT,
      success: (response) => {
        if (response.statusCode < 200 || response.statusCode >= 300) {
          reject(new Error(`HTTP ${response.statusCode}`))
          return
        }

        const payload = getResponsePayload(response.data)
        if (!payload || typeof payload !== 'object') {
          reject(new Error('invalid payload'))
          return
        }

        const snapshot = normalizeSnapshot(payload, url, {
          source: 'remote'
        })
        cacheSnapshot(snapshot)
        resolve(snapshot)
      },
      fail: (error) => {
        reject(error)
      }
    })
  })
}

function probeWaterApi(options = {}) {
  const apiUrl = options.apiUrl || readWaterApiUrl()
  return fetchRemoteSnapshot(apiUrl)
}

function getWaterQualitySnapshot(options = {}) {
  const apiUrl = options.apiUrl || readWaterApiUrl()

  return fetchRemoteSnapshot(apiUrl).catch((error) => {
    const cached = readCachedWaterSnapshot()
    return {
      ...cached,
      connected: false,
      stale: true,
      deviceIp: cached.deviceIp === '未连接' ? getApiHost(apiUrl) : cached.deviceIp,
      errorMessage: error && (error.message || error.errMsg) ? String(error.message || error.errMsg) : '水质数据暂不可用'
    }
  })
}

function evaluateMetric(key, value, connected) {
  if (!Number.isFinite(toNumber(value))) {
    return {
      status: connected ? '无数据' : '未连接',
      severity: connected ? 1 : 2,
      penalty: connected ? 6 : 12
    }
  }

  if ((key === 'ammonia' || key === 'nitrite' || key === 'nitrate') && Number(value) >= 20000) {
    return {
      status: '异常',
      severity: 2,
      penalty: 26
    }
  }

  if (key === 'waterTemp') {
    if (value >= 24 && value <= 30) return { status: '正常', severity: 0, penalty: 0 }
    if (value >= 22 && value <= 32) return { status: '关注', severity: 1, penalty: 6 }
    return { status: value < 24 ? '偏低' : '偏高', severity: 2, penalty: 14 }
  }

  if (key === 'ph') {
    if (value >= 6.5 && value <= 8.5) return { status: '稳定', severity: 0, penalty: 0 }
    if (value >= 6 && value <= 9) return { status: '关注', severity: 1, penalty: 8 }
    return { status: '异常', severity: 2, penalty: 16 }
  }

  if (key === 'conductivity') {
    if (value >= 200 && value <= 1800) return { status: '正常', severity: 0, penalty: 0 }
    if (value >= 0 && value <= 2500) return { status: '关注', severity: 1, penalty: 4 }
    return { status: '偏高', severity: 2, penalty: 12 }
  }

  if (key === 'tds') {
    if (value >= 100 && value <= 900) return { status: '正常', severity: 0, penalty: 0 }
    if (value >= 0 && value <= 1200) return { status: '关注', severity: 1, penalty: 4 }
    return { status: '偏高', severity: 2, penalty: 10 }
  }

  if (key === 'salinity') {
    if (value >= 0.2 && value <= 1.0) return { status: '正常', severity: 0, penalty: 0 }
    if (value >= 0 && value <= 2.0) return { status: '关注', severity: 1, penalty: 4 }
    return { status: '异常', severity: 2, penalty: 10 }
  }

  if (key === 'cod') {
    if (value <= 20) return { status: '低位', severity: 0, penalty: 0 }
    if (value <= 30) return { status: '关注', severity: 1, penalty: 8 }
    return { status: '偏高', severity: 2, penalty: 16 }
  }

  if (key === 'ammonia') {
    if (value <= 0.2) return { status: '正常', severity: 0, penalty: 0 }
    if (value <= 0.5) return { status: '关注', severity: 1, penalty: 10 }
    return { status: '偏高', severity: 2, penalty: 18 }
  }

  if (key === 'nitrite') {
    if (value <= 0.05) return { status: '正常', severity: 0, penalty: 0 }
    if (value <= 0.2) return { status: '关注', severity: 1, penalty: 12 }
    return { status: '偏高', severity: 2, penalty: 20 }
  }

  if (key === 'nitrate') {
    if (value <= 20) return { status: '正常', severity: 0, penalty: 0 }
    if (value <= 40) return { status: '关注', severity: 1, penalty: 8 }
    return { status: '累积', severity: 2, penalty: 14 }
  }

  return { status: '正常', severity: 0, penalty: 0 }
}

function buildTrendText(key, history) {
  const values = history
    .map((item) => toNumber(item.metricValues[key]))
    .filter((value) => Number.isFinite(value))

  if (values.length < 2) return '实时'

  const current = values[values.length - 1]
  const previous = values[values.length - 2]
  const delta = current - previous

  const thresholds = {
    waterTemp: 0.2,
    ph: 0.03,
    conductivity: 10,
    tds: 6,
    salinity: 0.02,
    cod: 0.2,
    ammonia: 0.01,
    nitrite: 0.005,
    nitrate: 0.1
  }

  const limit = thresholds[key] || 0.01
  if (Math.abs(delta) <= limit) return '平稳'
  return delta > 0 ? '上升' : '下降'
}

function buildMetricRows(snapshot) {
  const history = readHistory()

  return METRIC_META.map((meta) => {
    const rawValue = toNumber(snapshot.metricValues[meta.key])
    const evaluation = evaluateMetric(meta.key, rawValue, snapshot.connected)
    return {
      key: meta.key,
      name: meta.name,
      value: formatMetricValue(meta.key, rawValue),
      unit: meta.unit,
      tone: meta.tone,
      status: evaluation.status,
      percent: metricPercent(meta.key, rawValue),
      trend: buildTrendText(meta.key, history),
      rawValue,
      severity: evaluation.severity,
      penalty: evaluation.penalty
    }
  })
}

function buildMetricMap(metricRows) {
  return metricRows.reduce((map, item) => {
    map[item.key] = item
    return map
  }, {})
}

function buildQualityBands(metricRows) {
  const byKey = buildMetricMap(metricRows)
  const nitrogenRows = [byKey.ammonia, byKey.nitrite, byKey.nitrate]
  const worstNitrogen = nitrogenRows.sort((left, right) => {
    if (right.severity !== left.severity) {
      return right.severity - left.severity
    }
    return right.percent - left.percent
  })[0]

  return [
    { label: '酸碱', value: `${byKey.ph.value} ${byKey.ph.unit}`, level: byKey.ph.status, percent: byKey.ph.percent },
    { label: '有机', value: `${byKey.cod.value} ${byKey.cod.unit}`, level: byKey.cod.status, percent: byKey.cod.percent },
    { label: '氮盐', value: `${worstNitrogen.name} ${worstNitrogen.value} ${worstNitrogen.unit}`, level: worstNitrogen.status, percent: worstNitrogen.percent }
  ]
}

function buildAssessment(metricRows, connected, stale) {
  const validMetrics = metricRows.filter((item) => Number.isFinite(item.rawValue))
  const severeMetrics = validMetrics.filter((item) => item.severity >= 2)
  const watchMetrics = validMetrics.filter((item) => item.severity === 1)

  if (!connected && !validMetrics.length) {
    return {
      score: 0,
      riskLevel: '未连接',
      riskClass: 'safe',
      conclusion: '尚未接收到实时水质数据。',
      actions: [
        '启动采集服务。',
        '手机与网关保持同一网络。',
        '等待实时水质回传。'
      ],
      severeCount: 0,
      watchCount: 0
    }
  }

  let score = 100
  metricRows.forEach((item) => {
    score -= item.penalty
  })
  if (stale) {
    score -= 8
  }
  score = clamp(Math.round(score), 0, 100)

  let riskLevel = '稳定'
  let riskClass = 'safe'
  if (severeMetrics.length) {
    riskLevel = score <= 45 ? '异常' : '预警'
    riskClass = 'warn'
  } else if (watchMetrics.length) {
    riskLevel = '关注'
    riskClass = 'watch'
  }
  if (!connected) {
    riskLevel = '离线'
    riskClass = severeMetrics.length ? 'warn' : 'watch'
  }

  const priority = severeMetrics[0] || watchMetrics[0] || null
  let conclusion = '当前水质处于稳定区间，适合继续保持常规养殖策略。'
  if (!connected) {
    conclusion = '显示最近缓存数据，建议恢复连接后复核现场状态。'
  } else if (priority) {
    if (priority.key === 'cod') {
      conclusion = '当前有机负载偏高，建议先控制投喂并复核换水与增氧节奏。'
    } else if (priority.key === 'ammonia') {
      conclusion = '当前氨氮压力偏高，建议先减料并加强增氧，随后复测。'
    } else if (priority.key === 'nitrite') {
      conclusion = '当前亚硝酸根异常，说明氮循环存在压力，建议立即复核水体处理。'
    } else if (priority.key === 'nitrate') {
      conclusion = '当前硝酸根累积明显，建议结合换水计划继续观察趋势。'
    } else if (priority.key === 'ph') {
      conclusion = '当前酸碱状态偏离稳定区间，建议复核水体缓冲和现场环境。'
    } else {
      conclusion = '当前部分水质指标偏离稳定区间，建议先复核现场环境后再执行养殖操作。'
    }
  }

  const actions = []
  if (!connected) {
    actions.push('优先恢复水质采集服务，避免继续依据旧数据判断。')
  }
  if (metricRows.find((item) => item.key === 'ammonia' && item.severity >= 1)) {
    actions.push('投喂前复核氨氮状态，必要时先减料并加强增氧。')
  }
  if (metricRows.find((item) => item.key === 'nitrite' && item.severity >= 1)) {
    actions.push('重点关注亚硝酸根，必要时进行补换水并复测。')
  }
  if (metricRows.find((item) => item.key === 'cod' && item.severity >= 1)) {
    actions.push('控制残饵和有机负载，观察 COD 后续变化。')
  }
  if (metricRows.find((item) => item.key === 'nitrate' && item.severity >= 1)) {
    actions.push('持续跟踪硝酸根趋势，结合换水计划评估累积压力。')
  }
  if (metricRows.find((item) => item.key === 'ph' && item.severity >= 1)) {
    actions.push('复核 pH 变化，避免在异常波动期执行高强度投喂。')
  }
  while (actions.length < 3) {
    actions.push(actions.length === 0
      ? '保持现场巡检节奏，继续观察下一轮实时水质数据。'
      : '如现场状态稳定，可维持当前常规养殖策略并定时复测。')
  }

  return {
    score,
    riskLevel,
    riskClass,
    conclusion,
    actions: actions.slice(0, 3),
    severeCount: severeMetrics.length,
    watchCount: watchMetrics.length
  }
}

function buildAlertCards(metricRows, assessment, connected) {
  if (!connected && !metricRows.some((item) => Number.isFinite(item.rawValue))) {
    return [
      {
        level: '离线',
        tone: 'watch',
        title: '等待真实水质数据',
        detail: '当前尚未接收到网关返回的实时数据。'
      }
    ]
  }

  const riskyItems = metricRows
    .filter((item) => item.severity >= 1)
    .sort((left, right) => {
      if (right.severity !== left.severity) {
        return right.severity - left.severity
      }
      return right.percent - left.percent
    })

  if (!riskyItems.length) {
    return [
      {
        level: '正常',
        tone: 'ok',
        title: '当前无高危告警',
        detail: '核心水质指标处于相对稳定区间。'
      }
    ]
  }

  return riskyItems.slice(0, 2).map((item) => ({
    level: item.status,
    tone: item.severity >= 2 ? 'watch' : 'ok',
    title: `${item.name}需要关注`,
    detail: `${item.name} 当前值 ${item.value} ${item.unit}，建议结合现场环境复核。`
  }))
}

function buildOverviewStats(metricRows, assessment, connected) {
  const validCount = metricRows.filter((item) => Number.isFinite(item.rawValue)).length
  const nitrogenRows = metricRows.filter((item) => item.key === 'ammonia' || item.key === 'nitrite' || item.key === 'nitrate')
  const nitrogenRisk = nitrogenRows.reduce((best, item) => {
    if (!best) return item
    if (item.severity > best.severity) return item
    if (item.percent > best.percent) return item
    return best
  }, null)

  return [
    { label: '有效指标', value: `${validCount}/9`, desc: connected ? '实时采集' : '缓存回显', tone: 'green' },
    { label: '风险告警', value: `${assessment.severeCount}`, desc: '高危项', tone: 'cyan' },
    { label: '氮盐压力', value: nitrogenRisk ? nitrogenRisk.status : '--', desc: '氨氮/亚硝酸根/硝酸根', tone: 'blue' },
    { label: '建议动作', value: assessment.actions[0].indexOf('换水') >= 0 ? '换水' : '巡检', desc: '当前优先动作', tone: 'amber' }
  ]
}

function buildIndexWaterState(snapshot) {
  const metricRows = buildMetricRows(snapshot)
  const byKey = buildMetricMap(metricRows)
  const assessment = buildAssessment(metricRows, snapshot.connected, snapshot.stale)
  const currentUpdate = snapshot.updateTime || '--:--:--'

  return {
    connected: snapshot.connected,
    lastUpdate: currentUpdate,
    waterScore: assessment.score,
    riskLevel: assessment.riskLevel,
    deviceIp: snapshot.deviceIp || '未连接',
    metrics: metricRows,
    focusMetrics: [byKey.waterTemp, byKey.ph, byKey.cod, byKey.ammonia],
    nitrogenMetrics: [byKey.ammonia, byKey.nitrite, byKey.nitrate],
    conductivityMetrics: [byKey.conductivity, byKey.tds, byKey.salinity],
    organicMetrics: [byKey.cod],
    compositionMetrics: [byKey.conductivity, byKey.tds, byKey.salinity, byKey.cod],
    qualityBands: buildQualityBands(metricRows),
    industrialStatus: [
      { label: '采集网关', value: snapshot.connected ? '在线' : '离线', tone: snapshot.connected ? 'live' : 'standby' },
      { label: '数据源', value: snapshot.stale ? '缓存回显' : '实时采集', tone: snapshot.stale ? 'standby' : 'live' },
      { label: 'RS485总线', value: snapshot.connected ? '在线' : '待连接', tone: snapshot.connected ? 'live' : 'standby' },
      { label: '最近采样', value: currentUpdate, tone: snapshot.connected ? 'live' : 'standby' }
    ],
    alertCards: buildAlertCards(metricRows, assessment, snapshot.connected),
    opsChecks: [
      { label: '数据源', value: snapshot.connected ? '在线' : '离线' },
      { label: '有效指标', value: `${metricRows.filter((item) => Number.isFinite(item.rawValue)).length}/9` },
      { label: '高危项', value: `${assessment.severeCount}项` }
    ]
  }
}

function buildDeviceWaterState(snapshot) {
  const metricRows = buildMetricRows(snapshot)
  const byKey = buildMetricMap(metricRows)
  const assessment = buildAssessment(metricRows, snapshot.connected, snapshot.stale)
  const updatedAt = snapshot.updateTime || '--:--:--'

  const sensors = SENSOR_META.map((meta) => {
    const row = byKey[meta.key]
    let metricLabel = meta.metric
    if (meta.address === '02' && Number.isFinite(byKey.conductivity.rawValue)) {
      metricLabel = `pH/EC ${byKey.conductivity.value}uS/cm`
    }

    return {
      address: meta.address,
      name: meta.name,
      metric: metricLabel,
      value: row.value,
      unit: meta.unit,
      port: 'RS485',
      state: Number.isFinite(row.rawValue) ? '在线' : '离线',
      percent: row.percent || 6,
      update: updatedAt,
      tone: meta.tone
    }
  })

  return {
    deviceState: snapshot.connected ? '在线运行' : '等待数据',
    deviceIp: snapshot.deviceIp || '未连接',
    heartbeat: updatedAt,
    health: assessment.score,
    summaryStats: [
      { label: '有效指标', value: `${metricRows.filter((item) => Number.isFinite(item.rawValue)).length}/9`, desc: '真实水质', tone: 'cyan' },
      { label: '当前风险', value: assessment.riskLevel, desc: '综合研判', tone: 'green' },
      { label: '高危告警', value: `${assessment.severeCount}`, desc: '实时判断', tone: 'amber' },
      { label: '最近更新', value: updatedAt, desc: '水质快照', tone: 'blue' }
    ],
    sensors
  }
}

function buildReportWaterState(snapshot) {
  const metricRows = buildMetricRows(snapshot)
  const assessment = buildAssessment(metricRows, snapshot.connected, snapshot.stale)
  const selectedMetrics = [metricRows[1], metricRows[5], metricRows[6], metricRows[7], metricRows[8]]

  return {
    score: assessment.score,
    riskLevel: assessment.riskLevel,
    riskClass: assessment.riskClass,
    lastUpdateText: snapshot.updateTime || '--:--:--',
    conclusion: assessment.conclusion,
    overviewStats: buildOverviewStats(metricRows, assessment, snapshot.connected),
    metrics: selectedMetrics.map((item) => ({
      name: item.name,
      value: item.value,
      unit: item.unit === 'pH' ? '' : item.unit,
      percent: item.percent,
      state: item.status,
      tone: item.tone
    })),
    actions: assessment.actions
  }
}

module.exports = {
  DEFAULT_WATER_API_URL,
  WATER_REFRESH_MS,
  WATER_REQUEST_TIMEOUT,
  normalizeWaterApiUrl,
  readWaterApiUrl,
  persistWaterApiUrl,
  makeEmptySnapshot,
  readCachedWaterSnapshot,
  probeWaterApi,
  getWaterQualitySnapshot,
  buildIndexWaterState,
  buildDeviceWaterState,
  buildReportWaterState
}

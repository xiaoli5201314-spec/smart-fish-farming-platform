const { isLoggedIn } = require('../../utils/auth')
const waterQuality = require('../../utils/water-quality')

const QUICK_QUESTIONS = [
  '当前水质安全吗？',
  '适合继续投喂吗？',
  '硝酸根需要处理吗？',
  '今天需要换水吗？',
  '需要加强增氧吗？',
  'COD偏高怎么办？'
]

const AI_CONFIG_STORAGE_KEY = 'water_quality_ai_api_config'
const DEFAULT_AI_API_URL = 'http://192.0.2.10:8000/api/ai/chat'
const AI_REQUEST_TIMEOUT = 70000
const AI_HEALTH_TIMEOUT = 6000
const READY_STATE = 'AI在线'

const EMPTY_REPORT_WATER = waterQuality.buildReportWaterState(waterQuality.makeEmptySnapshot())

const INITIAL_MESSAGES = [
  {
    role: 'assistant',
    text: '我可以根据当前水质给出风险判断和处理建议。'
  }
]

function toNumber(value) {
  const number = Number(value)
  return Number.isFinite(number) ? number : value
}

function makeChatHistory(messages) {
  return messages
    .filter((item) => item && !item.thinking && (item.role === 'user' || item.role === 'assistant'))
    .slice(-6)
    .map((item) => ({
      role: item.role,
      text: item.text
    }))
}

function makeRequestErrorMessage(error) {
  const message = error && (error.message || error.errMsg)
  if (message) {
    return `AI服务暂不可用：${message}`
  }
  return 'AI服务暂不可用，请检查服务地址。'
}

function formatAiAnswer(answer) {
  return String(answer || '')
    .replace(/\*\*(.*?)\*\*/g, '$1')
    .replace(/^\s*[-*]\s+/gm, '')
    .replace(/\n{3,}/g, '\n\n')
    .trim()
}

function normalizeAiApiUrl(value) {
  let url = String(value || '').trim()
  if (!url) return DEFAULT_AI_API_URL

  if (!/^https?:\/\//i.test(url)) {
    url = `http://${url}`
  }

  url = url.replace(/\s/g, '')
  if (/\/api\/ai\/chat\/?$/i.test(url)) {
    return url.replace(/\/$/, '')
  }

  return `${url.replace(/\/+$/, '')}/api/ai/chat`
}

function makeAiHealthUrl(apiUrl) {
  const url = normalizeAiApiUrl(apiUrl)
  return url.replace(/\/api\/ai\/chat$/i, '/health')
}

function readAiApiUrl() {
  try {
    const stored = wx.getStorageSync(AI_CONFIG_STORAGE_KEY)
    if (stored && stored.apiUrl) {
      return normalizeAiApiUrl(stored.apiUrl)
    }
  } catch (error) {}

  return DEFAULT_AI_API_URL
}

Page({
  data: {
    score: EMPTY_REPORT_WATER.score,
    riskLevel: EMPTY_REPORT_WATER.riskLevel,
    riskClass: EMPTY_REPORT_WATER.riskClass,
    assistantState: READY_STATE,
    assistantChipText: READY_STATE,
    aiApiUrl: DEFAULT_AI_API_URL,
    aiConfigOpen: false,
    aiConfigState: '未测试',
    lastUpdateText: EMPTY_REPORT_WATER.lastUpdateText,
    conclusion: EMPTY_REPORT_WATER.conclusion,
    inputValue: '',
    thinking: false,
    quickQuestions: QUICK_QUESTIONS,
    overviewStats: EMPTY_REPORT_WATER.overviewStats,
    metrics: EMPTY_REPORT_WATER.metrics,
    actions: EMPTY_REPORT_WATER.actions,
    messages: INITIAL_MESSAGES,
    pageEnter: false
  },

  onLoad() {
    if (!this.ensureLogin()) return
    this.loadAiConfig()
    this.syncWaterQuality()
  },

  onShow() {
    if (!this.ensureLogin()) return
    this.playPageEnter()
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
    this.clearThinkingTimer()
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
    if (isLoggedIn()) return true

    wx.reLaunch({
      url: '/pages/auth/index'
    })
    return false
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

  syncWaterQuality() {
    waterQuality.getWaterQualitySnapshot().then((snapshot) => {
      this.setData(waterQuality.buildReportWaterState(snapshot))
    })
  },

  clearThinkingTimer() {
    if (this.thinkingTimer) {
      clearTimeout(this.thinkingTimer)
      this.thinkingTimer = null
    }
    if (this.aiRequestTask && this.aiRequestTask.abort) {
      this.aiRequestTask.abort()
      this.aiRequestTask = null
    }
    this.aiRequestId = (this.aiRequestId || 0) + 1
  },

  onInput(event) {
    this.setData({
      inputValue: event.detail.value
    })
  },

  onAiUrlInput(event) {
    this.setData({
      aiApiUrl: event.detail.value
    })
  },

  loadAiConfig() {
    this.setData({
      aiApiUrl: readAiApiUrl()
    })
  },

  toggleAiConfig() {
    this.setData({
      aiConfigOpen: !this.data.aiConfigOpen
    })
  },

  saveAiConfig(showToast = true) {
    const apiUrl = normalizeAiApiUrl(this.data.aiApiUrl)

    try {
      wx.setStorageSync(AI_CONFIG_STORAGE_KEY, { apiUrl })
    } catch (error) {}

    this.setData({
      aiApiUrl: apiUrl,
      aiConfigState: '已保存',
      assistantState: READY_STATE,
      assistantChipText: READY_STATE
    })

    if (showToast) {
      wx.showToast({
        title: '已保存',
        icon: 'success'
      })
    }

    return apiUrl
  },

  testAiService() {
    const apiUrl = this.saveAiConfig(false)
    const healthUrl = makeAiHealthUrl(apiUrl)

    this.setData({
      aiConfigState: '测试中',
      assistantState: '连接测试',
      assistantChipText: '测试中'
    })

    wx.request({
      url: healthUrl,
      method: 'GET',
      timeout: AI_HEALTH_TIMEOUT,
      success: (response) => {
        const ok = response.statusCode >= 200 && response.statusCode < 300 && response.data && response.data.ok
        this.setData({
          aiConfigState: ok ? '连接正常' : '连接异常',
          assistantState: ok ? READY_STATE : '连接异常',
          assistantChipText: ok ? READY_STATE : '重试'
        })

        wx.showToast({
          title: ok ? '连接正常' : '连接异常',
          icon: ok ? 'success' : 'none'
        })
      },
      fail: () => {
        this.setData({
          aiConfigState: '连接失败',
          assistantState: '连接异常',
          assistantChipText: '重试'
        })
        wx.showToast({
          title: '连接失败',
          icon: 'none'
        })
      }
    })
  },

  askQuick(event) {
    const question = event.currentTarget.dataset.question
    if (!question) return
    this.reply(question)
  },

  sendQuestion() {
    const question = this.data.inputValue.trim()
    if (!question || this.data.thinking) return

    this.setData({
      inputValue: ''
    })
    this.reply(question)
  },

  clearChat() {
    this.clearThinkingTimer()
    this.setData({
      inputValue: '',
      thinking: false,
      assistantState: READY_STATE,
      assistantChipText: READY_STATE,
      messages: INITIAL_MESSAGES
    })
  },

  makeWaterPayload() {
    return {
      score: this.data.score,
      riskLevel: this.data.riskLevel,
      conclusion: this.data.conclusion,
      updateTime: this.data.lastUpdateText,
      metrics: this.data.metrics.map((item) => ({
        name: item.name,
        value: toNumber(item.value),
        unit: item.unit,
        state: item.state,
        percent: item.percent
      })),
      actions: this.data.actions
    }
  },

  reply(question) {
    if (this.data.thinking) return

    const requestId = (this.aiRequestId || 0) + 1
    const history = makeChatHistory(this.data.messages)
    const messages = this.data.messages.concat([
      {
        role: 'user',
        text: question
      },
      {
        role: 'assistant',
        text: 'AI思考中',
        thinking: true
      }
    ]).slice(-8)

    this.setData({
      messages,
      thinking: true,
      assistantState: 'AI分析中',
      assistantChipText: '请求中'
    })

    this.aiRequestId = requestId
    this.aiRequestTask = wx.request({
      url: this.saveAiConfig(false),
      method: 'POST',
      timeout: AI_REQUEST_TIMEOUT,
      header: {
        'content-type': 'application/json'
      },
      data: {
        question,
        waterData: this.makeWaterPayload(),
        messages: history
      },
      success: (response) => {
        if (this.aiRequestId !== requestId) return

        const data = response.data || {}
        if (response.statusCode >= 200 && response.statusCode < 300 && data.ok && data.answer) {
          this.finishReply(formatAiAnswer(data.answer), READY_STATE, READY_STATE)
          return
        }

        this.finishReply(makeRequestErrorMessage(data), '服务异常', '重试')
      },
      fail: (error) => {
        if (this.aiRequestId !== requestId) return
        this.finishReply(makeRequestErrorMessage(error), '连接异常', '重试')
      },
      complete: () => {
        if (this.aiRequestId === requestId) {
          this.aiRequestTask = null
        }
      }
    })
  },

  finishReply(answer, assistantState = READY_STATE, assistantChipText = READY_STATE) {
    const nextMessages = this.data.messages.slice()
    const lastIndex = nextMessages.length - 1

    if (lastIndex >= 0 && nextMessages[lastIndex].thinking) {
      nextMessages[lastIndex] = {
        role: 'assistant',
        text: answer
      }
    } else {
      nextMessages.push({
        role: 'assistant',
        text: answer
      })
    }

    this.setData({
      messages: nextMessages.slice(-8),
      thinking: false,
      assistantState,
      assistantChipText
    })
  }
})

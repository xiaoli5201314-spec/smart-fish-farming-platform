const { isLoggedIn } = require('../../utils/auth')

const guideTabs = [
  { key: 'overview', name: '系统总览' },
  { key: 'pages', name: '页面使用' },
  { key: 'params', name: '指标说明' }
]

Page({
  data: {
    activeTab: 'overview',
    guideTabs,
    pageEnter: false,
    overviewStats: [
      { value: '4', label: '底部入口' },
      { value: '9', label: '水质指标' }
    ],
    systemCards: [
      {
        index: '01',
        title: '水质',
        desc: '综合水质、核心指标、实景视频和告警。'
      },
      {
        index: '02',
        title: '分析',
        desc: '评分、建议和水质问答。'
      },
      {
        index: '03',
        title: '后端',
        desc: '实时画面、现场作业控制和访问设置。'
      },
      {
        index: '04',
        title: '说明',
        desc: '页面结构、核心指标和操作说明。'
      }
    ],
    pageSteps: [
      {
        index: '01',
        title: '登录进入系统',
        desc: '登录后进入种鱼数智管理平台。'
      },
      {
        index: '02',
        title: '水质',
        desc: '水质概览、天气和关键指标。'
      },
      {
        index: '03',
        title: '分析',
        desc: '评分、建议和问答。'
      },
      {
        index: '04',
        title: '后端',
        desc: '打开实时画面并使用现场控制。'
      },
      {
        index: '05',
        title: '说明',
        desc: '查看页面结构和指标含义。'
      }
    ],
    parameterGroups: [
      {
        title: '基础环境',
        desc: '用于判断水体基础状态和鱼体适应性的核心指标。',
        items: [
          { mark: '温', name: '水温', tag: '代谢', text: '影响鱼体摄食、活动、代谢和应激反应。' },
          { mark: '酸', name: 'pH', tag: '酸碱', text: '反映水体酸碱环境，异常时会影响鱼体呼吸和氨氮毒性。' },
          { mark: '盐', name: '盐度', tag: '渗透压', text: '用于观察水体盐分水平和鱼体渗透压适应状态。' }
        ]
      },
      {
        title: '有机与溶解负载',
        desc: '用于判断水体污染压力和长期负荷变化。',
        items: [
          { mark: '导', name: '电导率', tag: '离子', text: '反映水中离子导电能力，可辅助判断矿物质和盐分变化。' },
          { mark: '固', name: 'TDS', tag: '溶解物', text: '反映总溶解固体含量，升高代表水体负载增加。' },
          { mark: '耗', name: 'COD', tag: '有机物', text: '反映有机污染压力，残饵、粪便和有机碎屑累积时容易升高。' }
        ]
      },
      {
        title: '氮盐风险',
        desc: '用于判断氮循环压力，是鱼塘水质预警里需要重点关注的指标。',
        items: [
          { mark: '氨', name: '氨氮', tag: '毒性', text: '主要来自排泄物和有机物分解，升高时会带来明显生理压力。' },
          { mark: '亚', name: '亚硝酸根', tag: '中间产物', text: '氮循环中间产物，异常升高说明硝化过程不稳定。' },
          { mark: '硝', name: '硝酸根', tag: '累积', text: '氮循环后端产物，长期累积代表过滤或换水压力增加。' }
        ]
      }
    ]
  },

  onShow() {
    if (isLoggedIn()) {
      this.playPageEnter()
      return
    }

    wx.reLaunch({
      url: '/pages/auth/index'
    })
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

  switchTab(event) {
    const { key } = event.currentTarget.dataset
    if (!key || key === this.data.activeTab) return

    this.setData({
      activeTab: key
    })
  }
})

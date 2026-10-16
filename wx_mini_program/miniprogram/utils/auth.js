const USERS_KEY = 'water_quality_users'
const SESSION_KEY = 'water_quality_session'

function normalizeUsername(username = '') {
  return String(username).trim()
}

function getUsers() {
  const users = wx.getStorageSync(USERS_KEY)
  return Array.isArray(users) ? users : []
}

function saveUsers(users = []) {
  wx.setStorageSync(USERS_KEY, users)
}

function getCurrentUser() {
  const session = wx.getStorageSync(SESSION_KEY)
  return session && session.username ? session : null
}

function isLoggedIn() {
  return !!getCurrentUser()
}

function registerUser(username, password) {
  const normalizedUsername = normalizeUsername(username)
  const normalizedPassword = String(password || '')

  if (!normalizedUsername) {
    return {
      ok: false,
      message: '请输入账号'
    }
  }

  if (normalizedPassword.length < 4) {
    return {
      ok: false,
      message: '密码至少 4 位'
    }
  }

  const users = getUsers()
  const existed = users.some((item) => item.username === normalizedUsername)

  if (existed) {
    return {
      ok: false,
      message: '该账号已注册'
    }
  }

  users.push({
    username: normalizedUsername,
    password: normalizedPassword,
    createdAt: Date.now()
  })
  saveUsers(users)

  return {
    ok: true
  }
}

function loginUser(username, password) {
  const normalizedUsername = normalizeUsername(username)
  const normalizedPassword = String(password || '')
  const users = getUsers()
  const matchedUser = users.find(
    (item) =>
      item.username === normalizedUsername &&
      item.password === normalizedPassword
  )

  if (!matchedUser) {
    return {
      ok: false,
      message: users.length ? '账号或密码错误' : '请先注册账号'
    }
  }

  const session = {
    username: matchedUser.username,
    loginAt: Date.now()
  }
  wx.setStorageSync(SESSION_KEY, session)

  return {
    ok: true,
    user: session
  }
}

function logoutUser() {
  wx.removeStorageSync(SESSION_KEY)
}

module.exports = {
  getCurrentUser,
  isLoggedIn,
  registerUser,
  loginUser,
  logoutUser
}

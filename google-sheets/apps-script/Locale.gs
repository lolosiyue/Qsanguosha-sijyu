// Stable English keys for fixed Sheets UI text. Dynamic game text comes from
// the native bridge and must not be translated or reconstructed here.
const QSAN_TEXT = Object.freeze({
  menuRoot: 'QSanGuosha',
  menuSetup: '建立專用工作表',
  menuConnect: '連線與操作控制',
  menuCatalog: '載入房間目錄',
  menuPreview: '預檢選擇',
  menuSubmit: '提交選擇',
  menuCancel: '取消／結束出牌',
  menuRetry: '重試待確認指令',
  menuDetails: '查詢詳情（座位／項目）',
  menuDisconnect: '離開並關閉會話',
  sidebarTitle: '三國殺・Sheets',
  busy: '正在更新或提交，請稍後再試。',
  stateTooLarge: '狀態超過儲存限制。',
  pendingTooLarge: '指令超過可恢復儲存上限，尚未送出。',
  pendingCorrupt: '待確認指令資料不完整，請保留會話。',
  commandIdCorrupt: '指令序號損壞。',
  commandIdFull: '指令序號已滿。',
  endpointInvalid: '請輸入有效 HTTPS 服務根網址。',
  pairRequired: '請先配對主機。',
  updateFailed: '牌桌更新失敗；可以安全重試更新，沒有送出遊戲指令。',
  commandUnknown: '遊戲指令結果未確認；請按「重試待確認指令」，不要重新操作。',
  shutdownUnknown: '關閉結果未確認；憑證仍保留，請重試離開並核對主機狀態。',
  pairUnknown: '配對結果未確認；請用相同網址與配對碼重試。',
  requestFailed: '服務請求失敗；請檢查連線後重試。',
  redirectRejected: '已拒絕重新導向；請核對服務網址。',
  invalidResponse: '回覆無效，結果未確認。',
  serviceUnavailable: '服務暫時無法處理請求。',
  alreadyPaired: '目前文件已配對，請先正常離開。',
  invalidPairCode: '配對碼格式不正確。',
  retryPair: '請重試原配對，或先清除已過期的配對。',
  invalidPairResponse: '配對回覆無效。',
  pairedLeave: '已配對的會話請正常離開。',
  pairCleared: '已清除待配對狀態；原生會話按主機期限回收。',
  commandPending: '前一指令未確認；請先按「重試待確認指令」。',
  identityMismatch: '回覆身分不符，保留原指令待確認。',
  nativeRejected: '原生拒絕操作。',
  retryConfirmed: '原指令已確認；請重新整理牌桌後繼續。',
  lastSuccess: '上次指令已成功確認，沒有重送。',
  lastRejected: '上次指令已拒絕：',
  noPending: '目前沒有待確認指令。',
  updateIdentityMismatch: '牌桌更新身分不符。',
  updated: '已更新',
  preflightExpired: '預檢已過期，請重新整理牌桌。',
  preflight: '預檢：',
  canConfirm: '可以確認',
  continueSelecting: '請繼續選擇',
  preflightPassed: '預檢通過，可提交。',
  incompleteSelection: '尚未完成選擇。',
  submitted: '已提交，等待伺服器更新。',
  cannotCancel: '目前互動不能取消。',
  cancelSent: '已送出取消／結束出牌。',
  operationSent: '已送出操作。',
  settingKeep: '保留',
  settingBool: '布林',
  settingInteger: '整數',
  settingList: '清單',
  hosting: '正在開啟私有對局。',
  shutdownIdentityMismatch: '關閉回覆身分不符，憑證仍保留。',
  sessionEndedUnclean: '會話已結束，但未正常退出，請檢查主機診斷。',
  sessionEndUnknown: '尚未確認會話已結束，憑證仍保留，請檢查主機診斷。'
});
function qsanText_(key) { return Object.prototype.hasOwnProperty.call(QSAN_TEXT, key) ? QSAN_TEXT[key] : String(key); }
// Table.gs currently writes the Traditional Chinese tokens; accept those
// legacy values and stable English IDs without changing stored worksheets.
const QSAN_SETTING_TYPES = Object.freeze({
  keep: ['保留', 'Keep'], bool: ['布林', 'Boolean'], integer: ['整數', 'Integer'], list: ['清單', 'List']
});
function qsanSettingType_(value, key) {
  return QSAN_SETTING_TYPES[key].indexOf(String(value)) >= 0;
}

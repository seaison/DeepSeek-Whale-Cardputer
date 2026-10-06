// 生成 tests/golden/peak-2026.tsv —— 峰谷判定的「对拍数据」。
//
// ⚠️ 本文件里的 isPeakTime / nextPeakChangeAt **逐行转录自上游**
//    MeteorNOX/DeepSeek-Balance-Whale-Widget (MIT) 的 lib/index.js
//    （PEAK_HOURS / WEEKEND_VALLEY_FROM_SEC / HOLIDAY_VALLEY /
//      HOLIDAY_VALLEY_FROM_SEC / isPeakTime / nextPeakChangeAt）。
//
//    存在的意义只有一个：让 C++ 移植版能对着**上游的真实实现**跑等价性测试，
//    而不是对着我自己重写一遍的规范自说自话。上游改了规则，这里要跟着改，
//    然后重新生成 golden（CI 会检查 golden 与生成器是否一致）。
//
// 用法：
//   node tests/gen-golden.mjs > tests/golden/peak-2026.tsv
//
// 输出格式（TSV，`#` 开头是注释）：
//   <北京日期> <6 位十六进制> <下一切换时刻 epoch 秒 | 0>
//   十六进制是 24 个小时的位图：第 h 位（bit h，LSB 起）为 1 表示该小时属高峰。
//   第三列 = nextPeakChangeAt(该日北京 00:00)。

const PEAK_HOURS = [
  [9, 12],
  [14, 18],
];
const WEEKEND_VALLEY_FROM_SEC = Math.floor(Date.UTC(2026, 7, 22, 16, 0, 0) / 1000);
const HOLIDAY_VALLEY_FROM_SEC = Math.floor(Date.UTC(2026, 8, 18, 16, 0, 0) / 1000);
const HOLIDAY_VALLEY = {
  '2026-01-01': 1, '2026-01-02': 1, '2026-01-03': 1,
  '2026-02-15': 1, '2026-02-16': 1, '2026-02-17': 1, '2026-02-18': 1, '2026-02-19': 1,
  '2026-02-20': 1, '2026-02-21': 1, '2026-02-22': 1, '2026-02-23': 1,
  '2026-04-04': 1, '2026-04-05': 1, '2026-04-06': 1,
  '2026-05-01': 1, '2026-05-02': 1, '2026-05-03': 1, '2026-05-04': 1, '2026-05-05': 1,
  '2026-06-19': 1, '2026-06-20': 1, '2026-06-21': 1,
  '2026-09-25': 1, '2026-09-26': 1, '2026-09-27': 1,
  '2026-10-01': 1, '2026-10-02': 1, '2026-10-03': 1, '2026-10-04': 1,
  '2026-10-05': 1, '2026-10-06': 1, '2026-10-07': 1,
};

function isHolidayValley(bjDate) {
  try {
    return !!HOLIDAY_VALLEY[bjDate.toISOString().slice(0, 10)];
  } catch (err) {
    return false;
  }
}

function isPeakTime(timeSec) {
  if (!isFinite(Number(timeSec))) return false;
  const n = Number(timeSec);
  const bj = new Date(n * 1000 + 8 * 3600 * 1000);
  if (n >= WEEKEND_VALLEY_FROM_SEC) {
    const dow = bj.getUTCDay();
    if (dow === 0 || dow === 6) return false;
  }
  if (n >= HOLIDAY_VALLEY_FROM_SEC && isHolidayValley(bj)) return false;
  const hour = bj.getUTCHours();
  for (const [start, end] of PEAK_HOURS) {
    if (hour >= start && hour < end) return true;
  }
  return false;
}

function nextPeakChangeAt(timeSec) {
  const n = Number(timeSec);
  if (!isFinite(n)) return null;
  const cur = isPeakTime(n);
  const day0 = Math.floor((n + 8 * 3600) / 86400) * 86400;
  for (let d = 0; d <= 12; d++) {
    for (const edge of [0, 9, 12, 14, 18]) {
      const cand = day0 + d * 86400 + edge * 3600 - 8 * 3600;
      if (cand <= n + 1) continue;
      if (isPeakTime(cand) !== cur) return cand;
    }
  }
  return null;
}

// 北京某日 00:00 的 epoch
function beijingMidnight(y, mo, d) {
  return Date.UTC(y, mo - 1, d) / 1000 - 8 * 3600;
}

const out = [];
out.push('# tests/golden/peak-2026.tsv —— 由 tests/gen-golden.mjs 生成，请勿手工编辑');
out.push('# 数据来源：上游 lib/index.js 的 isPeakTime / nextPeakChangeAt（MIT，见文件头）');
out.push('# 格式：<北京日期> <6 位十六进制小时位图> <下一切换时刻 epoch 秒|0>');
out.push('#   位图第 h 位（LSB 起）= 该小时属于高峰');

const startY = 2026;
const endY = 2027;
const endM = 1;
for (let y = startY; y <= endY; y++) {
  const lastMonth = y === endY ? endM : 12;
  for (let mo = 1; mo <= lastMonth; mo++) {
    const daysInMonth = new Date(Date.UTC(y, mo, 0)).getUTCDate();
    for (let d = 1; d <= daysInMonth; d++) {
      const t0 = beijingMidnight(y, mo, d);
      let bits = 0;
      for (let h = 0; h < 24; h++) {
        if (isPeakTime(t0 + h * 3600)) bits |= 1 << h;
      }
      const next = nextPeakChangeAt(t0);
      const hex = bits.toString(16).padStart(6, '0');
      const date = `${y}-${String(mo).padStart(2, '0')}-${String(d).padStart(2, '0')}`;
      out.push(`${date}\t${hex}\t${next === null ? 0 : next}`);
    }
  }
}
process.stdout.write(out.join('\n') + '\n');

// SPDX-License-Identifier: GPL-3.0-or-later
// Trends use actual measurement dates and corrected archive ownership, not
// Recorder's historical UI states. No network calls or external dependencies.
class ScaleArchiveTrend extends HTMLElement {
  constructor() {
    super();
    this.attachShadow({ mode: 'open' });
  }
  setConfig(config) {
    this.config = config;
    this.signature = null;
    if (this._hass) this.render();
  }
  set hass(hass) {
    this._hass = hass;
    if (this.config) this.render();
  }
  getCardSize() { return 4; }
  render() {
    const state = this._hass.states[this.config.entity];
    const records = state?.attributes.archive_history || [];
    const timeZone = this.config.time_zone || state?.attributes.archive_time_zone || this._hass.config?.time_zone || 'UTC';
    const signature = JSON.stringify([records, this.config, timeZone]);
    if (signature === this.signature) return;
    this.signature = signature;
    const key = this.config.metric;
    const cutoff = Date.now() - 168 * 3600000;
    const points = records.filter(r => r[key] != null)
      .map(r => ({ id: r.id, t: Date.parse(r.measured_at), y: Number(r[key]) }))
      .filter(r => Number.isFinite(r.t) && Number.isFinite(r.y) && r.t >= cutoff)
      .sort((a, b) => a.t - b.t);
    const unit = key === 'weight' ? 'kg' : '%';
    const precision = key === 'weight' ? 2 : 1;
    const date = t => new Intl.DateTimeFormat('zh-CN', {
      timeZone, month: '2-digit', day: '2-digit',
      hour: '2-digit', minute: '2-digit', hour12: false
    }).format(t);
    this.shadowRoot.innerHTML = `<style>
      :host{display:block} ha-card{padding:20px 20px 16px;color:var(--primary-text-color)}
      h2{font-size:20px;font-weight:400;margin:0 0 8px} .meta,.empty{color:var(--secondary-text-color);font-size:13px}
      svg{display:block;width:100%;height:auto;margin:12px 0} text{fill:var(--secondary-text-color);font:13px sans-serif}
      .line{fill:none;stroke:var(--primary-color,#03a9f4);stroke-width:3}
      .point{fill:var(--primary-color,#03a9f4);stroke:var(--card-background-color,#fff);stroke-width:2}
      .grid{stroke:var(--divider-color,#ddd)} .value{fill:var(--primary-text-color);font-weight:600}
      details{font-size:13px;margin-top:12px} summary{cursor:pointer;color:var(--primary-color)}
      .row{display:flex;justify-content:space-between;gap:12px;padding:6px 0;border-bottom:1px solid var(--divider-color,#eee)}
    </style><ha-card><h2></h2><div class="meta"></div><div class="plot"></div><details><summary>查看测量记录</summary><div class="rows"></div></details></ha-card>`;
    this.shadowRoot.querySelector('h2').textContent = this.config.title;
    this.shadowRoot.querySelector('.meta').textContent = `近7天 · ${points.length}次有效测量 · 按称重归档同步`;
    const plot = this.shadowRoot.querySelector('.plot');
    if (!points.length) {
      plot.textContent = state?.attributes.history_origin ? '近7天暂无已归档测量' : '正在等待归档数据';
      plot.className = 'empty';
      return;
    }
    const low = Math.min(...points.map(p => p.y));
    const high = Math.max(...points.map(p => p.y));
    const pad = Math.max((high - low) * .25, key === 'weight' ? .2 : .5);
    const min = low - pad, max = high + pad;
    const start = points[0].t, end = points[points.length - 1].t;
    const x = p => points.length === 1 || start === end ? 320 : 70 + (p.t - start) / (end - start) * 510;
    const y = p => 175 - (p.y - min) / (max - min) * 135;
    const grids = [0, 1, 2].map(i => {
      const value = min + (max - min) * i / 2, yy = 175 - i * 67.5;
      return `<line class="grid" x1="65" x2="590" y1="${yy}" y2="${yy}"/><text x="55" y="${yy+5}" text-anchor="end">${value.toFixed(precision)}</text>`;
    }).join('');
    const circles = points.map(p => `<circle class="point" cx="${x(p)}" cy="${y(p)}" r="5"><title>${date(p.t)} · ${p.y.toFixed(precision)} ${unit}</title></circle>${points.length <= 8 ? `<text class="value" x="${x(p)}" y="${y(p)-12}" text-anchor="middle">${p.y.toFixed(precision)}</text>` : ''}`).join('');
    plot.innerHTML = `<svg viewBox="0 0 640 230" role="img" aria-label="按有效称重记录绘制的近七天趋势"><text x="65" y="18">${unit}</text>${grids}<polyline class="line" points="${points.map(p=>`${x(p)},${y(p)}`).join(' ')}"/>${circles}<text x="65" y="210">${date(start)}</text><text x="590" y="210" text-anchor="end">${date(end)}</text></svg>`;
    const rows = this.shadowRoot.querySelector('.rows');
    for (const point of points) {
      const row = document.createElement('div'); row.className = 'row';
      const when = document.createElement('span'); when.textContent = date(point.t);
      const value = document.createElement('span'); value.textContent = `${point.y.toFixed(precision)} ${unit}`;
      row.append(when, value); rows.append(row);
    }
  }
}
if (!customElements.get('scale-archive-trend')) customElements.define('scale-archive-trend', ScaleArchiveTrend);
window.customCards = window.customCards || [];
window.customCards.push({type:'scale-archive-trend', name:'称重归档趋势', description:'随认领与归属纠正同步的体重及体脂趋势'});

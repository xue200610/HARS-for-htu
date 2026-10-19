/* ===== 医院挂号管理系统 · 前端公共库 ===== */

/* ---------- 登录态存储 ---------- */
const Store = {
    get token() { return localStorage.getItem('hb_token') || ''; },
    get user() {
        try { return JSON.parse(localStorage.getItem('hb_user') || 'null'); } catch (_) { return null; }
    },
    save(token, user) {
        localStorage.setItem('hb_token', token);
        localStorage.setItem('hb_user', JSON.stringify(user));
    },
    clear() {
        localStorage.removeItem('hb_token');
        localStorage.removeItem('hb_user');
    }
};

/* ---------- 接口请求 ---------- */
async function request(method, path, body) {
    const res = await fetch(path, {
        method,
        headers: {
            'Content-Type': 'application/json; charset=utf-8',
            ...(Store.token ? { Authorization: `Bearer ${Store.token}` } : {})
        },
        body: body === undefined ? undefined : JSON.stringify(body)
    });

    let json = null;
    try { json = await res.json(); } catch (_) { /* 非 JSON 响应 */ }

    if (!res.ok) {
        if (res.status === 401 && !path.includes('/auth/login')) {
            toast('登录已过期，请重新登录', 'err');
            setTimeout(() => { Store.clear(); location.href = '/login.html'; }, 900);
        }
        throw new Error(json?.message || `请求失败（HTTP ${res.status}）`);
    }
    return json?.data;
}

const api = {
    get: (p) => request('GET', p),
    post: (p, b) => request('POST', p, b),
    put: (p, b) => request('PUT', p, b),
    del: (p) => request('DELETE', p)
};

/* ---------- 页面守卫 ---------- */
function requireRole(...roles) {
    const user = Store.user;
    if (!Store.token || !user) {
        location.href = '/login.html';
        return null;
    }
    if (roles.length && !roles.includes(user.role)) {
        toast('当前账号无权访问该页面', 'err');
        setTimeout(() => { location.href = roleHome(user.role); }, 900);
        return null;
    }
    return user;
}

function roleHome(role) {
    if (role === 'admin') return '/admin.html';
    if (role === 'doctor') return '/doctor.html';
    return '/patient.html';
}

function roleName(role) {
    return { admin: '管理员', doctor: '医生', patient: '患者' }[role] || role;
}

function logout() {
    Store.clear();
    location.href = '/login.html';
}

/* ---------- 顶部导航 ---------- */
function mountNavbar(active) {
    const user = Store.user;
    const links = [
        { href: '/index.html', text: '门户首页', key: 'index' },
        ...(user?.role === 'patient' ? [{ href: '/patient.html', text: '在线挂号', key: 'patient' }] : []),
        ...(user?.role === 'doctor' ? [{ href: '/doctor.html', text: '医生工作站', key: 'doctor' }] : []),
        ...(user?.role === 'admin' ? [{ href: '/admin.html', text: '管理后台', key: 'admin' }] : [])
    ];

    const nav = document.createElement('div');
    nav.className = 'navbar';
    nav.innerHTML = `
        <div class="brand"><span class="logo">✚</span> 医院挂号管理系统</div>
        <div class="links">
            ${links.map(l => `<a href="${l.href}" class="${l.key === active ? 'active' : ''}">${l.text}</a>`).join('')}
        </div>
        <div class="user">
            ${user
                ? `<span>${roleName(user.role)} <b>${esc(user.name)}</b>${user.dept ? ' · ' + esc(user.dept) : ''}</span>
                   <button class="btn sm" onclick="logout()">退出</button>`
                : `<a class="btn sm primary" href="/login.html">登录</a>`}
        </div>`;
    document.body.prepend(nav);
}

/* ---------- 小工具 ---------- */
const $ = (sel, root = document) => root.querySelector(sel);
const $$ = (sel, root = document) => Array.from(root.querySelectorAll(sel));

function esc(text) {
    return String(text ?? '').replace(/[&<>"']/g, c => (
        { '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]
    ));
}

function qs(name) {
    return new URLSearchParams(location.search).get(name);
}

function money(v) {
    return v === null || v === undefined || v === '' ? '-' : `¥${Number(v).toFixed(2)}`;
}

function todayStr(offset = 0) {
    const d = new Date();
    d.setDate(d.getDate() + offset);
    return `${d.getFullYear()}-${String(d.getMonth() + 1).padStart(2, '0')}-${String(d.getDate()).padStart(2, '0')}`;
}

/** 状态 -> 徽标颜色 */
function statusClass(status) {
    return {
        '待就诊': 'blue', '候诊中': 'orange', '已就诊': 'green', '已取消': 'red',
        '正常': 'green', '停诊': 'red', '门诊': 'blue', '住院': 'orange', '出院': 'green'
    }[status] || '';
}

function badge(status) {
    return `<span class="badge ${statusClass(status)}">${esc(status || '-')}</span>`;
}

/** 是否已过就诊时间（用于禁用取消按钮） */
function isPast(apptTime) {
    return String(apptTime || '').slice(0, 10) < todayStr();
}

/* ---------- 提示条 ---------- */
function toast(message, type = 'info', ms = 2400) {
    let box = $('.toast-box');
    if (!box) {
        box = document.createElement('div');
        box.className = 'toast-box';
        document.body.appendChild(box);
    }
    const item = document.createElement('div');
    item.className = `toast ${type}`;
    item.textContent = message;
    box.appendChild(item);
    setTimeout(() => item.remove(), ms);
}

/* ---------- 弹窗 ---------- */
function openModal({ title, bodyHtml, confirmText = '确定', cancelText = '取消', onConfirm, wide = false, hideFooter = false }) {
    const mask = document.createElement('div');
    mask.className = 'modal-mask';
    mask.innerHTML = `
        <div class="modal" ${wide ? 'style="max-width:640px"' : ''}>
            <header><span>${esc(title)}</span><span class="x">✕</span></header>
            <div class="body">${bodyHtml}</div>
            ${hideFooter ? '' : `<footer>
                <button class="btn" data-act="cancel">${esc(cancelText)}</button>
                <button class="btn primary" data-act="ok">${esc(confirmText)}</button>
            </footer>`}
        </div>`;
    document.body.appendChild(mask);

    const close = () => mask.remove();
    mask.querySelector('.x').onclick = close;
    mask.querySelector('[data-act="cancel"]')?.addEventListener('click', close);
    mask.querySelector('[data-act="ok"]')?.addEventListener('click', async () => {
        const ok = onConfirm ? await onConfirm(mask) : true;
        if (ok !== false) close();
    });
    mask.addEventListener('click', e => { if (e.target === mask) close(); });
    return { el: mask, close };
}

/** 二次确认 */
function confirmBox(message, { title = '请确认', confirmText = '确定', danger = false } = {}) {
    return new Promise(resolve => {
        const m = openModal({
            title,
            bodyHtml: `<div style="font-size:14px">${esc(message)}</div>`,
            confirmText,
            onConfirm: () => { resolve(true); return true; }
        });
        m.el.querySelector('[data-act="cancel"]').addEventListener('click', () => resolve(false));
        m.el.querySelector('.x').addEventListener('click', () => resolve(false));
    });
}

/** 表单弹窗：fields = [{name,label,type,value,options,required,placeholder}] */
function formModal({ title, fields, confirmText = '保存', onSubmit, wide = false }) {
    const html = fields.map(f => {
        const value = f.value ?? '';
        if (f.type === 'select') {
            const opts = (f.options || []).map(o => {
                const val = typeof o === 'object' ? o.value : o;
                const label = typeof o === 'object' ? o.label : o;
                return `<option value="${esc(val)}" ${String(val) === String(value) ? 'selected' : ''}>${esc(label)}</option>`;
            }).join('');
            return `<label class="field"><span>${esc(f.label)}</span><select name="${f.name}">${opts}</select></label>`;
        }
        if (f.type === 'textarea') {
            return `<label class="field"><span>${esc(f.label)}</span><textarea name="${f.name}" rows="3">${esc(value)}</textarea></label>`;
        }
        return `<label class="field"><span>${esc(f.label)}</span>
            <input name="${f.name}" type="${f.type || 'text'}" value="${esc(value)}"
                   placeholder="${esc(f.placeholder || '')}" ${f.step ? `step="${f.step}"` : ''}></label>`;
    }).join('');

    const m = openModal({
        title,
        bodyHtml: `<form id="modal-form">${html}</form>`,
        confirmText,
        wide,
        onConfirm: async (mask) => {
            const form = mask.querySelector('#modal-form');
            const data = {};
            fields.forEach(f => {
                const input = form.querySelector(`[name="${f.name}"]`);
                data[f.name] = input ? input.value.trim() : '';
            });
            const missing = fields.find(f => f.required && !data[f.name]);
            if (missing) { toast(`请填写「${missing.label}」`, 'err'); return false; }
            try {
                await onSubmit(data);
                return true;
            } catch (err) {
                toast(err.message, 'err');
                return false;
            }
        }
    });
    return m;
}

/* ---------- 条形图 ---------- */
function renderBars(container, rows, { nameKey = 'name', valueKey = 'count', empty = '暂无数据' } = {}) {
    if (!rows || !rows.length) {
        container.innerHTML = `<div class="empty">${esc(empty)}</div>`;
        return;
    }
    const max = Math.max(...rows.map(r => Number(r[valueKey]) || 0), 1);
    container.innerHTML = `<div class="bars">` + rows.map(r => {
        const val = Number(r[valueKey]) || 0;
        return `<div class="bar-row">
            <div class="name" title="${esc(r[nameKey])}">${esc(r[nameKey])}</div>
            <div class="track"><div class="fill" style="width:${(val / max * 100).toFixed(1)}%"></div></div>
            <div class="val">${val}</div>
        </div>`;
    }).join('') + `</div>`;
}

/* ---------- 表格渲染 ---------- */
function renderTable(container, { columns, rows, empty = '暂无数据', actions }) {
    // 防御：漏传 rows 时立刻报出来，避免静默渲染成"暂无数据"
    if (!Array.isArray(rows)) {
        console.error('renderTable: 缺少 rows 参数（应为数组）', rows);
        container.innerHTML = '<div class="empty">数据加载异常：rows 参数缺失</div>';
        return;
    }
    if (!rows.length) {
        container.innerHTML = `<div class="empty">${esc(empty)}</div>`;
        return;
    }
    const head = columns.map(c => `<th>${esc(c.title)}</th>`).join('') + (actions ? '<th>操作</th>' : '');
    const body = rows.map((row, index) => {
        const tds = columns.map(c => `<td>${c.render ? c.render(row, index) : esc(row[c.key] ?? '-')}</td>`).join('');
        const act = actions ? `<td class="actions">${actions(row, index)}</td>` : '';
        return `<tr>${tds}${act}</tr>`;
    }).join('');
    container.innerHTML = `<div class="table-wrap"><table class="data"><thead><tr>${head}</tr></thead><tbody>${body}</tbody></table></div>`;
}

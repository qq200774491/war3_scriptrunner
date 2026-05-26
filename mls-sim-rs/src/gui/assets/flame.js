(function () {
    if (window.__mls_flame_inited) return;
    window.__mls_flame_inited = true;

    let snapshot = null;
    let hover = null;
    let cssW = 1200, cssH = 320;

    function fit(canvas) {
        const rect = canvas.getBoundingClientRect();
        const dpr = window.devicePixelRatio || 1;
        cssW = Math.max(800, rect.width | 0);
        cssH = Math.max(200, rect.height | 0);
        if (canvas.width !== cssW * dpr || canvas.height !== cssH * dpr) {
            canvas.width = cssW * dpr;
            canvas.height = cssH * dpr;
            canvas.getContext('2d').setTransform(dpr, 0, 0, dpr, 0, 0);
        }
    }

    function colorFor(name, depth) {
        let h = 0;
        for (let i = 0; i < name.length; i++) h = (h * 31 + name.charCodeAt(i)) | 0;
        const hue = ((h & 0xff) + depth * 23) % 360;
        return `hsl(${hue}, 55%, 50%)`;
    }

    function drawNode(ctx, node, x, y, w, totalSamples, rowH) {
        if (w < 0.5 || y > cssH) return;
        const name = node.name || node.id || '?';
        ctx.fillStyle = colorFor(name, y / rowH | 0);
        ctx.fillRect(x, y, Math.max(1, w - 1), rowH - 1);
        if (w > 40) {
            ctx.fillStyle = '#fff';
            ctx.font = '11px monospace';
            const maxChars = Math.floor((w - 6) / 6);
            ctx.fillText(name.substring(0, maxChars), x + 3, y + 14);
        }
        const mx = hover ? hover.x : -1;
        const my = hover ? hover.y : -1;
        if (mx >= x && mx < x + w && my >= y && my < y + rowH) {
            window.__mls_flame_hit = {
                name: name,
                id: node.id,
                count: node.count || 0,
                self_count: node.self_count || 0,
                depth: y / rowH | 0,
            };
        }
        // 递归渲染子节点
        const children = node.children || [];
        let cx = x;
        for (const c of children) {
            const cw = (c.count || 0) / Math.max(1, node.count || 1) * w;
            drawNode(ctx, c, cx, y + rowH, cw, totalSamples, rowH);
            cx += cw;
        }
    }

    function draw() {
        const canvas = document.getElementById('flame');
        if (!canvas) { requestAnimationFrame(draw); return; }
        fit(canvas);
        const ctx = canvas.getContext('2d');
        ctx.fillStyle = '#1a1a1a';
        ctx.fillRect(0, 0, cssW, cssH);

        const tip = document.getElementById('flame-tip');

        if (!snapshot || !snapshot.data || !snapshot.data.root || !snapshot.data.total_samples) {
            ctx.fillStyle = '#666';
            ctx.font = '13px system-ui';
            const hint = snapshot ?
                '（暂无 profile 数据，请点击「开始采样」并等几秒）' :
                '（请选择房间）';
            ctx.fillText(hint, 16, 28);
            if (tip) tip.style.display = 'none';
            requestAnimationFrame(draw);
            return;
        }

        window.__mls_flame_hit = null;
        const root = snapshot.data.root;
        const total = snapshot.data.total_samples || root.count || 1;
        const rowH = 20;
        drawNode(ctx, root, 0, 0, cssW, total, rowH);

        const hit = window.__mls_flame_hit;
        if (tip) {
            if (hit && hover) {
                tip.style.display = 'block';
                tip.style.left = (hit.depth * 0 + (hover ? hover.clientX + 12 : 0)) + 'px';
                tip.style.top = ((hover ? hover.clientY + 12 : 0)) + 'px';
                const pct = (hit.count / total * 100).toFixed(1);
                const selfPct = (hit.self_count / total * 100).toFixed(1);
                tip.innerHTML = `<b>${hit.name}</b><br/>` +
                    `<span style="color:#aaa">总: ${hit.count} 采样 (${pct}%)</span><br/>` +
                    `<span style="color:#aaa">自身: ${hit.self_count} 采样 (${selfPct}%)</span>`;
            } else {
                tip.style.display = 'none';
            }
        }

        requestAnimationFrame(draw);
    }

    window.renderFlame = function (snapJson) {
        snapshot = typeof snapJson === 'string' ? JSON.parse(snapJson) : snapJson;
    };

    document.addEventListener('mousemove', function (e) {
        const canvas = document.getElementById('flame');
        if (!canvas) return;
        const rect = canvas.getBoundingClientRect();
        if (e.clientX < rect.left || e.clientX > rect.right || e.clientY < rect.top || e.clientY > rect.bottom) {
            hover = null;
            return;
        }
        hover = {
            x: e.clientX - rect.left,
            y: e.clientY - rect.top,
            clientX: e.clientX,
            clientY: e.clientY,
        };
    });

    requestAnimationFrame(draw);
})();

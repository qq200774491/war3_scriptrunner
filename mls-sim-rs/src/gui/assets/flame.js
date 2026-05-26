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

    function flatten(node, depth, list) {
        list.push({ name: node.name || node.id || '?', start: node.start || 0, dur: node.duration || node.dur || 0, depth });
        if (node.children) for (const c of node.children) flatten(c, depth + 1, list);
    }

    function draw() {
        const canvas = document.getElementById('flame');
        if (!canvas) { requestAnimationFrame(draw); return; }
        fit(canvas);
        const ctx = canvas.getContext('2d');
        ctx.fillStyle = '#1a1a1a';
        ctx.fillRect(0, 0, cssW, cssH);

        const tip = document.getElementById('flame-tip');

        if (!snapshot || !snapshot.data || !snapshot.data.root) {
            ctx.fillStyle = '#666';
            ctx.font = '13px system-ui';
            ctx.fillText(snapshot ? '（暂无 profile 数据，请确认目标房间已启动 profiler）' : '（请选择房间）', 16, 28);
            if (tip) tip.style.display = 'none';
            requestAnimationFrame(draw);
            return;
        }

        const list = [];
        flatten(snapshot.data.root, 0, list);
        const total = snapshot.data.root.duration || snapshot.data.root.dur || 1;
        const rowH = 20;
        let hit = null;
        const mx = hover ? hover.x : -1;
        const my = hover ? hover.y : -1;

        for (const n of list) {
            const x = (n.start / total) * cssW;
            const w = Math.max(1, (n.dur / total) * cssW);
            const y = n.depth * rowH;
            if (y > cssH) continue;
            ctx.fillStyle = colorFor(n.name, n.depth);
            ctx.fillRect(x, y, w, rowH - 1);
            if (w > 40) {
                ctx.fillStyle = '#fff';
                ctx.font = '11px monospace';
                ctx.fillText(n.name.substring(0, Math.floor(w / 6)), x + 3, y + 14);
            }
            if (mx >= x && mx < x + w && my >= y && my < y + rowH) hit = n;
        }

        if (tip) {
            if (hit) {
                tip.style.display = 'block';
                tip.style.left = (mx + 12) + 'px';
                tip.style.top = (my + 12) + 'px';
                tip.textContent = `${hit.name} · ${(hit.dur * 1000).toFixed(2)} ms (depth ${hit.depth})`;
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
        hover = { x: e.clientX - rect.left, y: e.clientY - rect.top };
    });

    requestAnimationFrame(draw);
})();

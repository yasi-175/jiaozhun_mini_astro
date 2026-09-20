#!/usr/bin/env python3
"""Compare the completed 25600-step and 51200-step PEC experiments."""
import csv, json
from pathlib import Path
import numpy as np

ROOT = Path('/home/q/workspace/jiaozhun_mini_astro')
CONFIG = [('25600', 167772.16, 6), ('51200', 335544.32, 3)]


def analyze(period, period_counts, expected_cycles):
    base = ROOT / f'pec_{period}_experiment'
    result = {'period': period, 'curves': {}, 'whole': {}, 'folded': {}}
    for name in ('pec_off', 'pec_on'):
        d = np.genfromtxt(base / f'{name}.csv', delimiter=',', names=True)
        travel = d['encoder'][0] - d['encoder']
        error = d['position_error_arcsec']
        trend = np.polyval(np.polyfit(travel, error, 1), travel)
        residual = error - trend
        result['whole'][name] = {
            'rms': float(np.sqrt(np.mean(residual * residual))),
            'p2p': float(np.ptp(residual)),
            'mean_speed': float(np.mean(d['actual_speed_hz'][10:])),
        }
        grid = np.linspace(0, period_counts, 512, endpoint=False)
        curves = []
        for cycle in range(expected_cycles):
            mask = (travel >= cycle * period_counts) & (travel < (cycle + 1) * period_counts)
            if np.count_nonzero(mask) < 100:
                continue
            x = travel[mask] - cycle * period_counts
            y = error[mask]
            order = np.argsort(x)
            curve = np.interp(grid, x[order], y[order])
            curve -= np.polyval(np.polyfit(grid, curve, 1), grid)
            curve -= curve.mean()
            curves.append(curve)
        curves = np.asarray(curves)
        folded = curves.mean(axis=0)
        result['curves'][name] = {'x': travel[::max(1, len(travel)//16000)].tolist(),
                                  'y': residual[::max(1, len(residual)//16000)].tolist()}
        result['folded'][name] = {
            'x': grid.tolist(), 'y': folded.tolist(),
            'rms': float(np.sqrt(np.mean(folded * folded))),
            'p2p': float(np.ptp(folded)),
            'repeat_rms': float(np.sqrt(np.mean((curves - folded) ** 2))),
            'cycles': int(len(curves)),
        }
    return result


results = [analyze(*cfg) for cfg in CONFIG]
summary = ['period,mode,whole_rms_arcsec,whole_p2p_arcsec,folded_rms_arcsec,folded_p2p_arcsec,repeat_residual_rms_arcsec,mean_speed_hz']
for r in results:
    for mode in ('pec_off', 'pec_on'):
        w, f = r['whole'][mode], r['folded'][mode]
        summary.append(f"{r['period']},{mode},{w['rms']:.6f},{w['p2p']:.6f},{f['rms']:.6f},{f['p2p']:.6f},{f['repeat_rms']:.6f},{w['mean_speed']:.6f}")
(ROOT / 'pec_period_comparison_summary.csv').write_text('\n'.join(summary) + '\n')
(ROOT / 'pec_period_comparison.json').write_text(json.dumps(results, separators=(',', ':')))

series = []
for r in results:
    for mode in ('pec_off', 'pec_on'):
        series.append({'name': f"{r['period']} {mode}", 'period': r['period'], 'mode': mode,
                       'x': r['curves'][mode]['x'], 'y': r['curves'][mode]['y']})
payload = json.dumps(series, separators=(',', ':'))
html = r'''<!doctype html><meta charset="utf-8"><title>PEC period comparison</title><style>body{font:14px Arial;margin:14px;background:#f3f3f3}.bar,.panel{background:white;border:1px solid #bbb;padding:9px;margin-bottom:12px}canvas{width:100%;height:560px;cursor:crosshair;display:block}button,select{font-size:14px;margin-right:10px}.read{font-family:monospace;min-height:20px}</style><h2>25600 / 51200 PEC：完整去漂移曲线对比</h2><div class="bar"><label>周期 <select id="period"><option value="all">全部</option><option value="25600">25600（7.2分钟）</option><option value="51200">51200（14.4分钟）</option></select></label><label><input id="off" type="checkbox" checked> PEC关闭</label><label><input id="on" type="checkbox" checked> PEC开启</label><button id="reset">Reset zoom</button><span>滚轮放大，拖动平移，双击恢复</span></div><div class="panel"><canvas id="plot"></canvas><div id="read" class="read"></div></div><script>const D=__DATA__,plot=document.getElementById('plot'),read=document.getElementById('read'),period=document.getElementById('period'),off=document.getElementById('off'),on=document.getElementById('on');let S={x0:null,x1:null,drag:null};function shown(){return D.filter(s=>(period.value==='all'||s.period===period.value)&&((s.mode==='pec_off'&&off.checked)||(s.mode==='pec_on'&&on.checked)))}function ext(){let a=shown().flatMap(s=>s.x);return [Math.min(...a),Math.max(...a)]}function draw(){let w=plot.clientWidth,h=plot.clientHeight,d=devicePixelRatio||1;plot.width=w*d;plot.height=h*d;let g=plot.getContext('2d');g.scale(d,d);let z=ext();if(S.x0===null){S.x0=z[0];S.x1=z[1]}let ss=shown(),ys=ss.flatMap(s=>s.y),mn=Math.min(...ys),mx=Math.max(...ys),pad=(mx-mn)*.08||1;mn-=pad;mx+=pad;let L=70,R=20,T=30,B=45,pw=w-L-R,ph=h-T-B,px=x=>L+(x-S.x0)/(S.x1-S.x0)*pw,py=y=>T+(mx-y)/(mx-mn)*ph;g.fillStyle='#fff';g.fillRect(0,0,w,h);for(let i=0;i<=10;i++){let x=S.x0+(S.x1-S.x0)*i/10,X=px(x);g.strokeStyle='#ddd';g.beginPath();g.moveTo(X,T);g.lineTo(X,T+ph);g.stroke();g.fillStyle='#444';g.textAlign='center';g.fillText(Math.round(x),X,T+ph+20)}for(let i=0;i<=8;i++){let y=mn+(mx-mn)*i/8,Y=py(y);g.strokeStyle='#ddd';g.beginPath();g.moveTo(L,Y);g.lineTo(L+pw,Y);g.stroke();g.fillStyle='#444';g.textAlign='right';g.fillText(y.toFixed(1),L-8,Y+4)}ss.forEach(s=>{let color=s.mode==='pec_off'?'#d32f2f':'#1565c0';g.strokeStyle=color;g.globalAlpha=s.period==='25600'?.45:1;g.lineWidth=1.3;g.beginPath();s.x.forEach((x,i)=>i?g.lineTo(px(x),py(s.y[i])):g.moveTo(px(x),py(s.y[i])));g.stroke()});g.globalAlpha=1;g.fillStyle='#444';g.fillText('红=PEC关闭，蓝=PEC开启；实线=51200，半透明=25600',L,T-10);plot._p={L,pw}}plot.onwheel=e=>{e.preventDefault();let p=plot._p,x=S.x0+(e.offsetX-p.L)/p.pw*(S.x1-S.x0),f=e.deltaY<0?.75:1.333,n=(S.x1-S.x0)*f,z=ext();S.x0=Math.max(z[0],x-(x-S.x0)*f);S.x1=Math.min(z[1],S.x0+n);draw()};plot.onmousedown=e=>S.drag={x:e.offsetX,a:S.x0,b:S.x1};plot.onmouseup=()=>S.drag=null;plot.onmouseleave=()=>S.drag=null;plot.onmousemove=e=>{let p=plot._p;if(S.drag){let dx=(e.offsetX-S.drag.x)/p.pw*(S.drag.b-S.drag.a),z=ext();S.x0=Math.max(z[0],S.drag.a-dx);S.x1=Math.min(z[1],S.drag.b-dx);draw()}else{let x=S.x0+(e.offsetX-p.L)/p.pw*(S.x1-S.x0);read.textContent='累计编码器位移='+Math.round(x)}};function resetView(){S.x0=S.x1=null;draw()}document.getElementById('reset').onclick=resetView;plot.ondblclick=resetView;period.onchange=resetView;off.onchange=draw;on.onchange=draw;draw();</script>'''.replace('__DATA__',payload)
(ROOT / 'pec_25600_51200_detrended_interactive.html').write_text(html, encoding='utf-8')
for line in summary:
    print(line)
print(ROOT / 'pec_25600_51200_detrended_interactive.html')

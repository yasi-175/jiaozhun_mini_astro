#!/usr/bin/env python3
"""Create raw and detrended full three-period 51200-step PEC plots."""
import csv, json
from pathlib import Path
import numpy as np

ROOT = Path('/home/q/workspace/jiaozhun_mini_astro/pec_51200_experiment')


def read(name, detrend=False):
    with (ROOT / f'{name}.csv').open(newline='') as f:
        rows = list(csv.DictReader(f))
    enc = np.asarray([float(r['encoder']) for r in rows])
    travel = enc[0] - enc
    y = np.asarray([float(r['position_error_arcsec']) for r in rows])
    if detrend:
        y = y - np.polyval(np.polyfit(travel, y, 1), travel)
    stride = max(1, len(rows) // 18000)
    return {'name': name, 'x': travel[::stride].tolist(), 'y': y[::stride].tolist()}


def make(filename, detrend):
    payload = json.dumps([read('pec_off', detrend), read('pec_on', detrend)], separators=(',', ':'))
    title = '51200步 PEC：完整三个周期位置误差'
    suffix = '（已去除整段线性漂移）' if detrend else '（原始累计误差）'
    html = r'''<!doctype html><meta charset="utf-8"><title>51200 PEC overall</title><style>body{font:14px Arial;margin:14px;background:#f3f3f3;color:#222}.bar,.panel{background:#fff;border:1px solid #bbb;padding:9px;margin-bottom:12px}button{font-size:14px;margin-right:10px}canvas{width:100%;height:540px;display:block;cursor:crosshair}.read{font-family:monospace;min-height:22px}</style><h2>__TITLE__</h2><div class="bar"><button id="reset">Reset zoom</button><span>红色：PEC关闭；蓝色：PEC开启。滚轮局部放大，拖动平移，双击恢复。</span></div><div class="panel"><canvas id="plot"></canvas><div id="read" class="read"></div></div><script>const D=__DATA__,plot=document.getElementById('plot'),read=document.getElementById('read');let S={x0:null,x1:null,drag:null};function ext(){let a=D.flatMap(s=>s.x);return[Math.min(...a),Math.max(...a)]}function draw(){let w=plot.clientWidth,h=plot.clientHeight,d=devicePixelRatio||1;plot.width=w*d;plot.height=h*d;let g=plot.getContext('2d');g.scale(d,d);let e=ext();if(S.x0===null){S.x0=e[0];S.x1=e[1]}let yy=D.flatMap(s=>s.y),mn=Math.min(...yy),mx=Math.max(...yy),pad=(mx-mn)*.08||1;mn-=pad;mx+=pad;let L=70,R=20,T=30,B=45,pw=w-L-R,ph=h-T-B,px=x=>L+(x-S.x0)/(S.x1-S.x0)*pw,py=y=>T+(mx-y)/(mx-mn)*ph;g.fillStyle='#fff';g.fillRect(0,0,w,h);for(let i=0;i<=10;i++){let x=S.x0+(S.x1-S.x0)*i/10,X=px(x);g.strokeStyle='#ddd';g.beginPath();g.moveTo(X,T);g.lineTo(X,T+ph);g.stroke();g.fillStyle='#444';g.textAlign='center';g.fillText(Math.round(x),X,T+ph+20)}for(let i=0;i<=8;i++){let y=mn+(mx-mn)*i/8,Y=py(y);g.strokeStyle='#ddd';g.beginPath();g.moveTo(L,Y);g.lineTo(L+pw,Y);g.stroke();g.fillStyle='#444';g.textAlign='right';g.fillText(y.toFixed(1),L-8,Y+4)}D.forEach((s,j)=>{g.strokeStyle=j?'#1565c0':'#d32f2f';g.lineWidth=1.3;g.beginPath();s.x.forEach((x,i)=>i?g.lineTo(px(x),py(s.y[i])):g.moveTo(px(x),py(s.y[i])));g.stroke()});g.fillStyle='#d32f2f';g.fillText('红色：PEC关闭',L,T-10);g.fillStyle='#1565c0';g.fillText('蓝色：PEC开启',L+115,T-10);plot._p={L,pw}}plot.onwheel=e=>{e.preventDefault();let p=plot._p,x=S.x0+(e.offsetX-p.L)/p.pw*(S.x1-S.x0),f=e.deltaY<0?.75:1.333,n=(S.x1-S.x0)*f,z=ext();S.x0=Math.max(z[0],x-(x-S.x0)*f);S.x1=Math.min(z[1],S.x0+n);draw()};plot.onmousedown=e=>S.drag={x:e.offsetX,a:S.x0,b:S.x1};plot.onmouseup=()=>S.drag=null;plot.onmouseleave=()=>S.drag=null;plot.onmousemove=e=>{let p=plot._p;if(S.drag){let dx=(e.offsetX-S.drag.x)/p.pw*(S.drag.b-S.drag.a),z=ext();S.x0=Math.max(z[0],S.drag.a-dx);S.x1=Math.min(z[1],S.drag.b-dx);draw()}else{let x=S.x0+(e.offsetX-p.L)/p.pw*(S.x1-S.x0),v=D.map(s=>{let i=0;for(let k=1;k<s.x.length;k++)if(Math.abs(s.x[k]-x)<Math.abs(s.x[i]-x))i=k;return s.y[i]});read.textContent='累计编码器位移='+Math.round(x)+'  PEC关闭='+v[0].toFixed(3)+'"  PEC开启='+v[1].toFixed(3)+'"'}};function reset(){S.x0=S.x1=null;draw()}document.getElementById('reset').onclick=reset;plot.ondblclick=reset;draw();</script>'''.replace('__TITLE__', title + suffix).replace('__DATA__', payload)
    (ROOT / filename).write_text(html, encoding='utf-8')


make('pec_off_on_overall_interactive.html', False)
make('pec_off_on_overall_detrended_interactive.html', True)
print(ROOT / 'pec_off_on_overall_interactive.html')
print(ROOT / 'pec_off_on_overall_detrended_interactive.html')

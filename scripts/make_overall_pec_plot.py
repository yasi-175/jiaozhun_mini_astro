#!/usr/bin/env python3
"""Create an interactive full six-period PEC-off/on plot."""
import csv, json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1] / "pec_25600_experiment"

def read(name):
    with (ROOT / f"{name}.csv").open(newline="") as f:
        rows = list(csv.DictReader(f))
    # Keep browser payload small while preserving the complete curve.
    stride = max(1, len(rows) // 16000)
    rows = rows[::stride]
    return {
        "name": name,
        "x": [float(r["encoder"]) for r in rows],
        "travel": [float(rows[0]["encoder"]) - float(r["encoder"]) for r in rows],
        "t": [float(r["elapsed_ms"]) / 1000.0 for r in rows],
        "y": [float(r["position_error_arcsec"]) for r in rows],
    }

d = json.dumps([read("pec_off"), read("pec_on")], separators=(",", ":"))
html = r'''<!doctype html><meta charset="utf-8"><title>PEC full six-period comparison</title>
<style>body{font-family:Arial,sans-serif;margin:14px;background:#f3f3f3;color:#222}.bar,.panel{background:#fff;border:1px solid #bbb;padding:9px;margin-bottom:12px}button,select{font-size:14px;margin-right:10px}canvas{width:100%;height:520px;display:block;cursor:crosshair}.read{font-family:monospace;min-height:22px}</style>
<h2>PEC 关闭/开启：完整六周期位置误差</h2><div class="bar"><label>X轴 <select id="xmode"><option value="travel">累计编码器位移</option><option value="time">时间</option><option value="encoder">绝对编码器位置</option></select></label><button id="reset">Reset zoom</button><span>滚轮局部放大，拖动平移，双击恢复</span></div><div class="panel"><canvas id="plot"></canvas><div id="read" class="read"></div></div>
<script>const D=__DATA__,COL={pec_off:'#d32f2f',pec_on:'#1565c0'};const plot=document.getElementById('plot'),read=document.getElementById('read'),xmode=document.getElementById('xmode');let S={x0:null,x1:null,drag:null};
function xv(s,i){return xmode.value==='time'?s.t[i]:xmode.value==='encoder'?s.x[i]:s.travel[i]}
function ext(){let a=D.flatMap(s=>s.x.map((_,i)=>xv(s,i)));return [Math.min(...a),Math.max(...a)]}
function draw(){let c=plot,w=c.clientWidth,h=c.clientHeight,d=devicePixelRatio||1;c.width=w*d;c.height=h*d;let g=c.getContext('2d');g.scale(d,d);let e=ext();if(S.x0==null){S.x0=e[0];S.x1=e[1]}let ys=D.flatMap(s=>s.y),mn=Math.min(...ys),mx=Math.max(...ys),pad=(mx-mn)*.08;mn-=pad;mx+=pad;let L=70,R=20,T=30,B=45,pw=w-L-R,ph=h-T-B,px=x=>L+(x-S.x0)/(S.x1-S.x0)*pw,py=y=>T+(mx-y)/(mx-mn)*ph;g.fillStyle='#fff';g.fillRect(0,0,w,h);g.font='12px Arial';for(let i=0;i<=10;i++){let x=S.x0+(S.x1-S.x0)*i/10,X=px(x);g.strokeStyle='#ddd';g.beginPath();g.moveTo(X,T);g.lineTo(X,T+ph);g.stroke();g.fillStyle='#444';g.textAlign='center';g.fillText(xmode.value==='time'?x.toFixed(0)+' s':Math.round(x),X,T+ph+22)}for(let i=0;i<=8;i++){let y=mn+(mx-mn)*i/8,Y=py(y);g.strokeStyle='#ddd';g.beginPath();g.moveTo(L,Y);g.lineTo(L+pw,Y);g.stroke();g.fillStyle='#444';g.textAlign='right';g.fillText(y.toFixed(1),L-8,Y+4)}D.forEach(s=>{g.strokeStyle=COL[s.name];g.lineWidth=1.2;g.beginPath();let started=false;s.y.forEach((y,i)=>{let x=xv(s,i);if(x<S.x0||x>S.x1)return;started?g.lineTo(px(x),py(y)):g.moveTo(px(x),py(y));started=true});g.stroke()});g.fillStyle=COL.pec_off;g.fillText('红色：PEC关闭',L,T-10);g.fillStyle=COL.pec_on;g.fillText('蓝色：PEC开启',L+115,T-10);c._p={L,pw};}
plot.onwheel=e=>{e.preventDefault();let p=plot._p,x=S.x0+(e.offsetX-p.L)/p.pw*(S.x1-S.x0),f=e.deltaY<0?.75:1.333,n=(S.x1-S.x0)*f;let a=ext();S.x0=Math.max(a[0],x-(x-S.x0)*f);S.x1=Math.min(a[1],S.x0+n);draw()};plot.onmousedown=e=>S.drag={x:e.offsetX,a:S.x0,b:S.x1};plot.onmouseup=()=>S.drag=null;plot.onmouseleave=()=>S.drag=null;plot.onmousemove=e=>{let p=plot._p;if(S.drag){let dx=(e.offsetX-S.drag.x)/p.pw*(S.drag.b-S.drag.a);let a=ext();S.x0=Math.max(a[0],S.drag.a-dx);S.x1=Math.min(a[1],S.drag.b-dx);draw()}else{let x=S.x0+(e.offsetX-p.L)/p.pw*(S.x1-S.x0),best=D.map(s=>{let i=0;for(let k=1;k<s.y.length;k++)if(Math.abs(xv(s,k)-x)<Math.abs(xv(s,i)-x))i=k;return s.y[i]});read.textContent='x='+x.toFixed(1)+'  PEC关闭='+best[0].toFixed(3)+'"  PEC开启='+best[1].toFixed(3)+'"'}};function resetView(){S.x0=S.x1=null;draw()}document.getElementById('reset').onclick=resetView;plot.ondblclick=resetView;xmode.onchange=resetView;draw();</script>'''.replace('__DATA__', d)
(ROOT / "pec_off_on_overall_interactive.html").write_text(html, encoding="utf-8")
print(ROOT / "pec_off_on_overall_interactive.html")

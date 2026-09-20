#!/usr/bin/env python3
"""Create a browser-based zoomable comparison plot for cycle CSV files."""
import csv, json
from pathlib import Path

root = Path(__file__).resolve().parents[1] / "dec_position_error_test"
files = sorted(root.glob("dec_position_error_cycle_*.csv"))
data = []
for path in files:
    with path.open(newline="") as f:
        rows = list(csv.DictReader(f))
    # Keep enough points for local detail while keeping browser load reasonable.
    stride = max(1, len(rows) // 12000)
    rows = rows[::stride]
    if rows[-1] != rows[-1]:
        rows.append(rows[-1])
    start = float(rows[0]["encoder"])
    data.append({
        "name": path.stem.replace("dec_position_error_", "").replace("_", " "),
        "start": start,
        "x": [float(r["encoder"]) for r in rows],
        "rel": [start - float(r["encoder"]) for r in rows],
        "err": [float(r["position_error_arcsec"]) for r in rows],
        "speed": [float(r["actual_speed_hz"]) for r in rows],
    })

payload = json.dumps(data, separators=(",", ":"))
html = r'''<!doctype html>
<meta charset="utf-8">
<title>DEC four-cycle local zoom comparison</title>
<style>
body{font-family:Arial,sans-serif;margin:14px;background:#f4f4f4;color:#222}
h2{margin:4px 0 8px}.toolbar{display:flex;gap:14px;align-items:center;flex-wrap:wrap;background:white;padding:9px;border:1px solid #bbb}
button,select{font-size:14px;padding:4px 8px}.hint{color:#555}.panel{background:white;border:1px solid #aaa;margin-top:12px;padding:6px}
canvas{display:block;width:100%;height:390px;cursor:crosshair}.readout{font-family:monospace;color:#333;min-height:20px}
</style>
<h2>DEC position-error / actual-speed four-cycle comparison</h2>
<div class="toolbar"><label>X axis <select id="xmode"><option value="absolute">Absolute encoder position</option><option value="relative">Counts from each cycle start</option></select></label>
<button id="reset">Reset zoom</button><span class="hint">Wheel: local zoom · drag: pan · double-click: reset · move mouse for values</span></div>
<div class="panel"><b>Position error</b><canvas id="err"></canvas><div id="errread" class="readout"></div></div>
<div class="panel"><b>Actual speed</b><canvas id="speed"></canvas><div id="speedread" class="readout"></div></div>
<script>
const DATA=__PAYLOAD__;
const COLORS=['#1565c0','#d32f2f','#2e7d32','#ef6c00'];
const state={xmode:'absolute',x0:null,x1:null,drag:null};
function xval(s,i){return state.xmode==='absolute'?s.x[i]:s.rel[i]}
function allX(){return DATA.flatMap(s=>s.xmode==='absolute'?s.x:s.rel)}
function extent(){let x=allX();return [Math.min(...x),Math.max(...x)]}
function draw(id,key,readid){
 const c=document.getElementById(id), dpr=devicePixelRatio||1, w=c.clientWidth, h=c.clientHeight;
 c.width=w*dpr;c.height=h*dpr;let g=c.getContext('2d');g.scale(dpr,dpr);
 let [xmin,xmax]=state.x0==null?extent():[state.x0,state.x1]; let ys=DATA.flatMap(s=>s[key]); let ymin=Math.min(...ys),ymax=Math.max(...ys);let p=(ymax-ymin)*.08||1;ymin-=p;ymax+=p;
 const L=66,R=18,T=28,B=42,pw=w-L-R,ph=h-T-B,px=x=>L+(x-xmin)/(xmax-xmin)*pw,py=y=>T+(ymax-y)/(ymax-ymin)*ph;
 g.clearRect(0,0,w,h);g.fillStyle='#fff';g.fillRect(0,0,w,h);g.font='12px Arial';g.strokeStyle='#ddd';g.fillStyle='#444';
 for(let i=0;i<=10;i++){let x=xmin+(xmax-xmin)*i/10,xx=px(x);g.beginPath();g.moveTo(xx,T);g.lineTo(xx,T+ph);g.stroke();g.textAlign='center';g.fillText(state.xmode==='absolute'?Math.round(x):Math.round(x/1000)+'k',xx,T+ph+20)}
 for(let i=0;i<=8;i++){let y=ymin+(ymax-ymin)*i/8,yy=py(y);g.beginPath();g.moveTo(L,yy);g.lineTo(L+pw,yy);g.stroke();g.textAlign='right';g.fillText(y.toFixed(1),L-8,yy+4)}
 g.strokeStyle='#333';g.beginPath();g.moveTo(L,T);g.lineTo(L,T+ph);g.lineTo(L+pw,T+ph);g.stroke();
 DATA.forEach((s,j)=>{g.strokeStyle=COLORS[j];g.lineWidth=1.3;g.beginPath();s[key].forEach((y,i)=>{let x=px(xval(s,i));i?g.lineTo(x,py(y)):g.moveTo(x,py(y))});g.stroke();g.fillStyle=COLORS[j];g.fillText('Cycle '+(j+1),L+j*90,T-9)});
 c._plot={xmin,xmax,ymin,ymax,L,T,pw,ph,key,readid};
}
function redraw(){draw('err','err','errread');draw('speed','speed','speedread')}
function zoomCanvas(c,factor,cx){let p=c._plot, x=p.xmin+(cx-p.L)/p.pw*(p.xmax-p.xmin), span=(p.xmax-p.xmin)*factor;state.x0=x-(x-p.xmin)*factor;state.x1=state.x0+span;let [a,b]=extent();state.x0=Math.max(a,state.x0);state.x1=Math.min(b,state.x1);if(state.x1-state.x0<1) return;redraw()}
function hooks(id){let c=document.getElementById(id);c.addEventListener('wheel',e=>{e.preventDefault();zoomCanvas(c,e.deltaY<0?.75:1.333,e.offsetX)},{passive:false});c.addEventListener('dblclick',()=>{state.x0=state.x1=null;redraw()});c.addEventListener('mousedown',e=>{state.drag={c,x:e.offsetX,x0:state.x0,x1:state.x1}});c.addEventListener('mouseup',()=>state.drag=null);c.addEventListener('mouseleave',()=>{state.drag=null});c.addEventListener('mousemove',e=>{let p=c._plot;if(state.drag){let dx=(e.offsetX-state.drag.x)/p.pw*(p.xmax-p.xmin);if(state.drag.x0!=null){state.x0=state.drag.x0-dx;state.x1=state.drag.x1-dx;let[a,b]=extent();if(state.x0<a){state.x1+=a-state.x0;state.x0=a}if(state.x1>b){state.x0-=state.x1-b;state.x1=b}}redraw()}else{let x=p.xmin+(e.offsetX-p.L)/p.pw*(p.xmax-p.xmin);let vals=DATA.map(s=>{let i=0;for(let k=1;k<s.x.length;k++)if(Math.abs(xval(s,k)-x)<Math.abs(xval(s,i)-x))i=k;return s[keySafe(p.key)][i]});document.getElementById(p.readid).textContent='x='+Math.round(x)+'  '+p.key+': '+vals.map((v,i)=>'C'+(i+1)+'='+v.toFixed(3)).join('  ')}})}
function keySafe(k){return k}
document.getElementById('xmode').onchange=e=>{state.xmode=e.target.value;state.x0=state.x1=null;redraw()};document.getElementById('reset').onclick=()=>{state.x0=state.x1=null;redraw()};hooks('err');hooks('speed');redraw();
</script>
'''.replace('__PAYLOAD__', payload)
(root / "interactive_cycle_comparison.html").write_text(html, encoding="utf-8")
print(root / "interactive_cycle_comparison.html")

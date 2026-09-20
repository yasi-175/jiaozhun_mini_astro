#!/usr/bin/env python3
"""Analyze and plot PEC-off versus PEC-on experiment CSV files."""

import csv
import json
import math
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[1] / "pec_25600_experiment"
PERIOD_COUNTS = 167772.16
BINS = 512


def load(name):
    data = np.genfromtxt(ROOT / f"{name}.csv", delimiter=",", names=True)
    travel = data["encoder"][0] - data["encoder"]
    return data, travel


def analyze(name):
    data, travel = load(name)
    error = data["position_error_arcsec"]
    grid = np.linspace(0.0, PERIOD_COUNTS, BINS, endpoint=False)
    curves = []
    cycle_stats = []
    for cycle in range(6):
        mask = (travel >= cycle * PERIOD_COUNTS) & (travel < (cycle + 1) * PERIOD_COUNTS)
        x = travel[mask] - cycle * PERIOD_COUNTS
        y = error[mask]
        order = np.argsort(x)
        curve = np.interp(grid, x[order], y[order])
        trend = np.polyval(np.polyfit(grid, curve, 1), grid)
        residual = curve - trend
        residual -= residual.mean()
        curves.append(residual)
        cycle_stats.append({
            "cycle": cycle + 1,
            "rms_arcsec": float(np.sqrt(np.mean(residual * residual))),
            "peak_to_peak_arcsec": float(np.ptp(residual)),
        })
    curves = np.asarray(curves)
    folded = curves.mean(axis=0)
    whole_trend = np.polyval(np.polyfit(travel, error, 1), travel)
    whole = error - whole_trend
    return {
        "name": name,
        "grid": grid,
        "curves": curves,
        "folded": folded,
        "cycle_stats": cycle_stats,
        "whole_rms": float(np.sqrt(np.mean(whole * whole))),
        "whole_p2p": float(np.ptp(whole)),
        "folded_rms": float(np.sqrt(np.mean(folded * folded))),
        "folded_p2p": float(np.ptp(folded)),
        "repeat_residual_rms": float(np.sqrt(np.mean((curves - folded) ** 2))),
        "mean_speed": float(np.mean(data["actual_speed_hz"][10:])),
    }


def write_html(off, on):
    series = []
    for result in (off, on):
        for index, curve in enumerate(result["curves"]):
            series.append({"name": f"{result['name']} C{index + 1}",
                           "group": result["name"], "x": result["grid"].tolist(),
                           "y": curve.tolist(), "folded": False})
        series.append({"name": f"{result['name']} folded mean", "group": result["name"],
                       "x": result["grid"].tolist(), "y": result["folded"].tolist(),
                       "folded": True})
    payload = json.dumps(series, separators=(",", ":"))
    html = r'''<!doctype html><meta charset="utf-8"><title>PEC 25600 comparison</title>
<style>body{font-family:Arial,sans-serif;margin:14px;background:#f3f3f3;color:#222}.bar,.panel{background:#fff;border:1px solid #bbb;padding:9px;margin-bottom:12px}button,label{margin-right:12px}canvas{width:100%;height:520px;display:block;cursor:crosshair}.read{font-family:monospace;min-height:22px}</style>
<h2>PEC 25600: detrended six-period comparison</h2><div class="bar"><button id="reset">Reset zoom</button><label><input id="cycles" type="checkbox" checked> individual cycles</label><label><input id="means" type="checkbox" checked> folded means</label><span>Wheel: zoom | drag: pan | double-click: reset</span></div><div class="panel"><canvas id="plot"></canvas><div id="read" class="read"></div></div>
<script>const D=__DATA__,C={pec_off:'#c62828',pec_on:'#1565c0'};let S={x0:0,x1:167772.16,drag:null};
function draw(){let c=plot,w=c.clientWidth,h=c.clientHeight,d=devicePixelRatio||1;c.width=w*d;c.height=h*d;let g=c.getContext('2d');g.scale(d,d);let shown=D.filter(s=>(s.folded?means.checked:cycles.checked));let ys=shown.flatMap(s=>s.y),mn=Math.min(...ys),mx=Math.max(...ys),pad=(mx-mn)*.08;mn-=pad;mx+=pad;let L=65,R=20,T=28,B=42,pw=w-L-R,ph=h-T-B,px=x=>L+(x-S.x0)/(S.x1-S.x0)*pw,py=y=>T+(mx-y)/(mx-mn)*ph;g.fillStyle='#fff';g.fillRect(0,0,w,h);g.font='12px Arial';for(let i=0;i<=10;i++){let x=S.x0+(S.x1-S.x0)*i/10,X=px(x);g.strokeStyle='#ddd';g.beginPath();g.moveTo(X,T);g.lineTo(X,T+ph);g.stroke();g.fillStyle='#444';g.textAlign='center';g.fillText(Math.round(x),X,T+ph+20)}for(let i=0;i<=8;i++){let y=mn+(mx-mn)*i/8,Y=py(y);g.strokeStyle='#ddd';g.beginPath();g.moveTo(L,Y);g.lineTo(L+pw,Y);g.stroke();g.fillStyle='#444';g.textAlign='right';g.fillText(y.toFixed(1),L-7,Y+4)}shown.forEach(s=>{g.strokeStyle=C[s.group];g.globalAlpha=s.folded?1:.22;g.lineWidth=s.folded?3:1;g.beginPath();let started=false;s.x.forEach((x,i)=>{if(x<S.x0||x>S.x1)return;let X=px(x),Y=py(s.y[i]);started?g.lineTo(X,Y):g.moveTo(X,Y);started=true});g.stroke()});g.globalAlpha=1;g.fillStyle=C.pec_off;g.fillText('red: PEC off',L,T-9);g.fillStyle=C.pec_on;g.fillText('blue: PEC on',L+100,T-9);c._p={L,pw};}
plot.onwheel=e=>{e.preventDefault();let p=plot._p,x=S.x0+(e.offsetX-p.L)/p.pw*(S.x1-S.x0),f=e.deltaY<0?.75:1.333,n=(S.x1-S.x0)*f;S.x0=Math.max(0,x-(x-S.x0)*f);S.x1=Math.min(167772.16,S.x0+n);draw()};plot.onmousedown=e=>S.drag={x:e.offsetX,a:S.x0,b:S.x1};plot.onmouseup=()=>S.drag=null;plot.onmouseleave=()=>S.drag=null;plot.onmousemove=e=>{let p=plot._p;if(S.drag){let dx=(e.offsetX-S.drag.x)/p.pw*(S.drag.b-S.drag.a);S.x0=Math.max(0,S.drag.a-dx);S.x1=Math.min(167772.16,S.drag.b-dx);draw()}else{let x=S.x0+(e.offsetX-p.L)/p.pw*(S.x1-S.x0),i=Math.max(0,Math.min(511,Math.round(x/167772.16*512)));let a=D.find(s=>s.name==='pec_off folded mean').y[i],b=D.find(s=>s.name==='pec_on folded mean').y[i];read.textContent=`phase=${Math.round(x)} counts, bin=${i}, off=${a.toFixed(2)} arcsec, on=${b.toFixed(2)} arcsec`}};function reset(){S.x0=0;S.x1=167772.16;draw()}reset.onclick=reset;plot.ondblclick=reset;cycles.onchange=draw;means.onchange=draw;draw();</script>'''.replace("__DATA__", payload)
    (ROOT / "pec_off_on_interactive.html").write_text(html, encoding="utf-8")


def main():
    off, on = analyze("pec_off"), analyze("pec_on")
    write_html(off, on)
    reduction = lambda a, b: (1.0 - b / a) * 100.0
    lines = [
        "metric,pec_off,pec_on,reduction_percent",
        f"whole_detrended_rms_arcsec,{off['whole_rms']:.4f},{on['whole_rms']:.4f},{reduction(off['whole_rms'], on['whole_rms']):.2f}",
        f"folded_repeatable_rms_arcsec,{off['folded_rms']:.4f},{on['folded_rms']:.4f},{reduction(off['folded_rms'], on['folded_rms']):.2f}",
        f"folded_repeatable_peak_to_peak_arcsec,{off['folded_p2p']:.4f},{on['folded_p2p']:.4f},{reduction(off['folded_p2p'], on['folded_p2p']):.2f}",
        f"nonrepeatable_residual_rms_arcsec,{off['repeat_residual_rms']:.4f},{on['repeat_residual_rms']:.4f},{reduction(off['repeat_residual_rms'], on['repeat_residual_rms']):.2f}",
        f"mean_actual_speed_hz,{off['mean_speed']:.6f},{on['mean_speed']:.6f},",
    ]
    (ROOT / "analysis_summary.csv").write_text("\n".join(lines) + "\n")
    print("\n".join(lines))
    print(ROOT / "pec_off_on_interactive.html")


if __name__ == "__main__":
    main()

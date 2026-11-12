// ============================================================================
// landing.h — landingspagina "/" van de ACCESS-sketch (sinds v0.2.4)
// Eenvoudige pagina voor huisgenoten en gasten: modus, dag/nacht, grondplan met alle
// buitenlampen en deuren, tegels per zone (aan/uit en dimmen). Technische pagina: /advanced.
//
// Eén pagina voor ACCESS1, 2 en 3: het grondplan bevat ALLE zones (D, W4, S, W1, N1, N2, E)
// en deuren (VD, SD, AD, BD) op een vaste plaats per code. Elke controller kleurt en bedient
// alleen de zones en deuren die in zijn /settings staan (CFG); de rest blijft grijs.
//
// Apart .h-bestand (zoals ROOM): de Arduino IDE laat JavaScript in een .h ongemoeid.
// Opbouw (zie handleLanding() in ESP32_C6_ACCESS.ino):
//   LANDING_KOP + naam + LANDING_MIDDEN + <script>var CFG={...}</script> + LANDING_SCRIPT
// Gebruikt alleen bestaande adressen: /status_json, /set_zone.
// ============================================================================
#pragma once
#include <Arduino.h>

const char LANDING_KOP[] PROGMEM = R"HTML(<!DOCTYPE html><html><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Zarlar buitenlicht</title><style>
body{font-family:Arial,sans-serif;margin:0;background:#fff;color:#222}
.hdr{background:#ffcc00;padding:12px 16px;font-weight:bold;font-size:20px}
.hdr span{font-weight:normal;font-size:14px}
.box{padding:12px;max-width:520px;margin:auto}
.rij{display:flex;gap:10px;margin:4px 0 10px}
.pil{flex:1;height:46px;border-radius:23px;border:2px solid #ccc;background:#fff;font-size:15px;display:flex;align-items:center;justify-content:center;gap:6px}
.pil.auto{background:#d8f5d8;border-color:#2a9d2a;color:#1d6b1d}
.pil.man{background:#ffe2c2;border-color:#e07b00;color:#8a4a00;cursor:pointer}
svg{width:100%;height:auto;display:block}
.zl{fill:none;stroke-width:8;stroke-linecap:round;stroke-linejoin:round}
.tegels{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:10px;margin-top:8px}
.tegel{border:2px solid #ccc;border-radius:12px;padding:10px;background:#fff;user-select:none;-webkit-user-select:none}
.tegel.aan{background:#fff4d6;border-color:#e0a000}
.tegel .kop{display:flex;justify-content:space-between;align-items:center;font-weight:bold;cursor:pointer}
.tegel .sub{font-size:13px;color:#555;margin-top:2px;min-height:17px}
.dot{width:12px;height:12px;border-radius:6px;background:#ccc;display:inline-block}
.dot.mov{background:#d00}
input[type=range]{width:100%;height:34px;margin-top:6px}
.deur{font-size:14px;margin:3px 0}
.uitleg{font-size:12px;color:#666;text-align:center;margin-top:10px}
.voet{text-align:center;margin:16px 0 8px}
.voet a{font-size:26px;text-decoration:none}
</style></head><body>
<div class="hdr">)HTML";

const char LANDING_MIDDEN[] PROGMEM = R"HTML( <span>&middot; buitenlicht</span></div><div class="box">
<div class="rij"><div class="pil" id="modus" onclick="naarAuto()">&hellip;</div><div class="pil" id="dagnacht">&hellip;</div></div>
<svg viewBox="0 0 340 330" id="plan">
<path d="M80 30 H300 V260 H320 V300 H80 V215 H45 V135 H80 Z" fill="#f3f3f3" stroke="#888" stroke-width="1.5"/>
<text x="190" y="19" text-anchor="middle" font-size="11" fill="#888">straatzijde (N)</text>
<text x="215" y="322" text-anchor="middle" font-size="11" fill="#888">tuinzijde (S)</text>
<text x="16" y="175" text-anchor="middle" font-size="11" fill="#888" transform="rotate(-90 16 175)">veldzijde (W)</text>
<text x="330" y="150" text-anchor="middle" font-size="11" fill="#888" transform="rotate(90 330 150)">binnenplaats (E)</text>
<text x="44" y="230" text-anchor="middle" font-size="10" fill="#999">afdak</text>
<g id="plz"></g><g id="pld"></g>
</svg>
<div class="tegels" id="tegels"></div>
<div class="uitleg">Tik op een lamp of een tegel: aan / uit &middot; schuifje: dimmen &middot; oranje modus: tik om terug naar automatisch te gaan</div>
<div class="voet"><span id="ver" style="font-size:11px;color:#999;margin-right:8px"></span><a href="/advanced" title="Technische pagina">&#9881;&#65039;</a></div>
</div>
)HTML";

const char LANDING_SCRIPT[] PROGMEM = R"HTML(<script>
var $=function(i){return document.getElementById(i);},bezig=0,laatst=null;
// Vaste plaats van elke zone en deur op het grondplan (FiDels tekening, 12/11/2026)
var ZONES={
 D:{n:'voordeur',p:'M80 120 L45 135 V215 L80 235',x:45,y:195},
 W4:{n:'veldkant',p:'M80 250 V290',x:80,y:270},
 S:{n:'terras',p:'M110 300 H310',x:200,y:300},
 W1:{n:'parking',p:'M80 40 V80',x:80,y:60},
 N1:{n:'puntgevel rechts',p:'M110 30 H160',x:135,y:30},
 N2:{n:'puntgevel links',p:'M215 30 H265',x:240,y:30},
 E:{n:'binnenplaats',p:'M300 60 V230',x:300,y:145}};
var DEUREN={VD:{n:'voordeur',x:63,y:152,w:12,h:18},SD:{n:'schuifdeur',x:125,y:294,w:18,h:12},
 AD:{n:'achterdeur',x:294,y:76,w:12,h:18},BD:{n:'bovendeur',x:268,y:76,w:12,h:18}};
function ns(t,a){var e=document.createElementNS('http://www.w3.org/2000/svg',t);for(var k in a)e.setAttribute(k,a[k]);return e;}
function slot(c){return CFG.codes.indexOf(c);}
function bouw(){
 var g=$('plz'),h='';
 for(var c in ZONES){var z=ZONES[c],eigen=slot(c)>=0;
  var gr=ns('g',{id:'pz'+c,opacity:eigen?1:0.35});if(eigen){gr.style.cursor='pointer';gr.onclick=(function(k){return function(){wissel(k);};})(c);}
  gr.appendChild(ns('path',{d:z.p,'class':'zl',stroke:'#bbb',id:'pl'+c}));
  gr.appendChild(ns('circle',{cx:z.x,cy:z.y,r:14,fill:'#fff',stroke:'#999','stroke-width':1.5,id:'pc'+c}));
  var t=ns('text',{x:z.x,y:z.y+4,'text-anchor':'middle','font-size':12,fill:'#333'});t.textContent=c;gr.appendChild(t);
  gr.appendChild(ns('circle',{cx:z.x+12,cy:z.y-12,r:4.5,fill:'#d00',id:'pm'+c,visibility:'hidden'}));
  g.appendChild(gr);
  if(eigen)h+='<div class="tegel" id="t'+c+'"><div class="kop" onclick="wissel(\''+c+'\')"><span>'+c+' &middot; '+z.n+'</span><span class="dot" id="d'+c+'"></span></div>'
   +'<div class="sub" id="s'+c+'">&hellip;</div><input type="range" min="0" max="100" step="1" id="r'+c+'" onchange="dim(\''+c+'\',this.value)"></div>';}
 var gd=$('pld'),dh='';
 for(var d in DEUREN){var q=DEUREN[d],eigen=CFG.deuren.indexOf(d)>=0;
  var gr=ns('g',{opacity:eigen?1:0.35});
  gr.appendChild(ns('rect',{x:q.x,y:q.y,width:q.w,height:q.h,rx:2,fill:'#eee',stroke:'#999',id:'pd'+d}));
  var t=ns('text',{x:q.x+q.w/2,y:q.y-3,'text-anchor':'middle','font-size':10,fill:'#555'});t.textContent=d;gr.appendChild(t);
  gd.appendChild(gr);
  if(eigen)dh+='<div class="deur"><span class="dot" id="dd'+d+'"></span> '+d+' '+q.n+': <b id="dt'+d+'">&hellip;</b></div>';}
 if(dh)h+='<div class="tegel"><div class="kop"><span>Deuren</span></div>'+dh+'</div>';
 $('tegels').innerHTML=h;}
function toon(d){laatst=d;
 var m=$('modus');m.className='pil '+(d.au?'auto':'man');
 m.textContent=d.au?'Automatisch':'Manueel · nog '+Math.ceil(d.ov/60)+' min';
 $('dagnacht').textContent=(d.nt?'☾ Nacht':'☀ Dag')+(d.lux>=0?' · '+d.lux+' lux':'');
 for(var i=0;i<CFG.codes.length;i++){var c=CFG.codes[i];if(!c||!ZONES[c]||!d.z[i])continue;var z=d.z[i],aan=z.l>0;
  var a=0.35+0.65*z.l/100;
  $('pl'+c).setAttribute('stroke',aan?'rgba(239,159,39,'+a+')':'#bbb');
  $('pc'+c).setAttribute('fill',aan?'rgba(250,199,117,'+a+')':'#fff');
  $('pc'+c).setAttribute('stroke',aan?'#ba7517':'#999');
  $('pm'+c).setAttribute('visibility',z.n>0?'visible':'hidden');
  var t=$('t'+c);if(t){t.className='tegel'+(aan?' aan':'');
   $('d'+c).className='dot'+(z.n>0?' mov':'');
   $('s'+c).textContent=(aan?z.l+' %':'uit')+(z.m?' · manueel':(z.k?' · beweging':''))+' · '+z.n+'/min';
   var r=$('r'+c);if(document.activeElement!==r)r.value=z.l;}}
 for(var j=0;j<CFG.deuren.length;j++){var dc=CFG.deuren[j];if(!dc||!DEUREN[dc]||!d.d[j])continue;var x=d.d[j];
  $('pd'+dc).setAttribute('fill',x.o?'#f7c1c1':'#c0dd97');$('pd'+dc).setAttribute('stroke',x.o?'#a32d2d':'#3b6d11');
  var e=$('dt'+dc);if(e){e.textContent=x.o?'open':'dicht';e.style.color=x.o?'#c00':'#2a7a2a';$('dd'+dc).style.background=x.o?'#d00':'#2a9d2a';}}}
function ververs(){if(bezig)return;fetch('/status_json?'+Date.now(),{cache:'no-store'}).then(function(r){return r.json();}).then(toon).catch(function(){});}
function knop(u){bezig=1;fetch(u).then(function(){bezig=0;ververs();}).catch(function(){bezig=0;});}
function wissel(c){var i=slot(c);if(i<0||!laatst||!laatst.z[i])return;knop('/set_zone?z='+c+'&v='+(laatst.z[i].l>0?0:1));}
function dim(c,v){knop('/set_zone?z='+c+(v>0?'&v=1&b='+v:'&v=0'));}
function naarAuto(){if(!laatst||laatst.au)return;
 for(var i=0;i<laatst.z.length;i++)if(laatst.z[i].m&&CFG.codes[i]){knop('/set_zone?z='+CFG.codes[i]+'&v=0');return;}}
$('ver').textContent=CFG.ver||'';bouw();ververs();setInterval(ververs,2000);
</script></body></html>)HTML";

// Local-only native integration peer; production avatar path, no production session.
import { writeFileSync } from 'node:fs';
const ws=new WebSocket('ws://127.0.0.1:8993/ws');
const report:any={frames:0,nativePose:null,chat:false};
let tick:ReturnType<typeof setInterval>|undefined,phase=0;
const output='/Users/olena/unrealtest/eidoverse/native_wire_report.json';
ws.onopen=()=>ws.send(JSON.stringify({type:'join',world:'underwater-prototype',id:'eidoverse-test-peer',avatar:'eidoverse/assets/vrms/claude.vrm?v=1786600887000'}));
ws.onmessage=e=>{
 const m=JSON.parse(String(e.data));
 if(m.type==='snapshot')tick=setInterval(()=>{
  phase+=.06;
  ws.send(JSON.stringify({type:'pose',pose:{p:[-24+Math.sin(phase)*2,-7+Math.sin(phase*.5),74],q:[0,0,0,1],yaw:0,speed:1,clip:'walk',locomotion:{mode:'swim',medium:'water',body:'diver'}}}));
 },67);
 if(m.type==='frame'&&m.poses?.['unreal-diver']){report.frames++;report.nativePose=m.poses['unreal-diver'];writeFileSync(output,JSON.stringify(report,null,2));}
 if(m.type==='log'&&m.entry?.verb==='say'&&m.entry.actor==='unreal-diver'){
  report.chat=true;writeFileSync(output,JSON.stringify(report,null,2));
  ws.send(JSON.stringify({type:'verb',verb:'say',args:{text:'Underwater link received. I can see your 3D pose.'}}));
 }
};
function stop(){if(tick)clearInterval(tick);ws.close();writeFileSync(output,JSON.stringify(report,null,2));}
process.on('SIGTERM',()=>{stop();process.exit(0);});
setTimeout(()=>{stop();process.exit(report.frames&&report.chat?0:1);},300000);

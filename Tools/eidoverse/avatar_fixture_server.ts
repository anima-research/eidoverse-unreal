// Local-only controllable protocol fixture. Only avatar bytes come from production.
// Run with bun; POST /step/{swap,update,sub,diver,leave} controls test events.
const library='eidoverse/assets/vrms/';
const claude=library+'claude.vrm?v=1786600887000';
const suit=library+'claude_suit.vrm?v=1786600887000';
const peers=new Set<any>();
let body='diver',tick=0;
let motion='hover',motionSince=Date.now();
function send(ws:any,x:any){ws.send(JSON.stringify(x));}
function broadcast(x:any){for(const ws of peers)send(ws,x);}
const server=Bun.serve({hostname:'127.0.0.1',port:18994,
 fetch(req,server){
  const url=new URL(req.url);
  if(url.pathname==='/ws'&&server.upgrade(req))return;
  if(req.method==='POST'&&url.pathname.startsWith('/step/')){
   const step=url.pathname.slice(6);
   if(['hover','swim','dive','ascend','surface','walk'].includes(step)){motion=step;motionSince=Date.now();}
   else if(step==='swap'){broadcast({type:'arrive',id:'avatar-a',avatar:suit});}
   else if(step==='update'){broadcast({type:'avatar-updated',path:library+'claude_suit.vrm',v:1786600887001});}
   else if(step==='sub')body='submarine';
   else if(step==='diver')body='diver';
   else if(step==='leave'){for(const id of ['avatar-a','avatar-b','avatar-bad'])broadcast({type:'leave',id});}
   else return new Response('unknown step',{status:400});
   return new Response('ok');
  }
  return new Response('local avatar protocol fixture');
 },
 websocket:{
  message(ws,data){const m=JSON.parse(String(data));if(m.type!=='join')return;
   peers.add(ws);
   send(ws,{type:'snapshot',you:m.id,throughSeq:0,present:[
    {id:'avatar-a',avatar:claude},{id:'avatar-b',avatar:claude},
    {id:'avatar-bad',avatar:library+'__missing_avatar_test__.vrm'}
   ]});
  },close(ws){peers.delete(ws);}
 }});
setInterval(()=>{tick++;
 const t=Math.min((Date.now()-motionSince)/1000,8);
 const moving=motion!=='hover';
 const vertical=motion==='dive'?-.6:motion==='ascend'?.6:0;
 broadcast({type:'frame',t:Date.now()/1000,poses:{
 'avatar-a':{p:[-24,-7+vertical*t,74+(moving?.45*t:0)],q:[0,0,0,1],yaw:0,speed:moving?.8:0,clip:moving?'walk':'idle',locomotion:{mode:body==='submarine'?'pilot':motion==='surface'?'surface':motion==='walk'?'walk':'swim',body,medium:motion==='walk'?'air':'water'}},
 'avatar-b':{p:[-26,-7,76],yaw:0,speed:0,clip:'idle',locomotion:{mode:'swim',body:'diver',medium:'water'}},
 'avatar-bad':{p:[-28,-7,78],yaw:0,speed:0,clip:'idle'}
}});},67);
console.log('AVATAR_FIXTURE_READY ws://127.0.0.1:18994/ws');
process.on('SIGTERM',()=>{server.stop(true);process.exit(0);});

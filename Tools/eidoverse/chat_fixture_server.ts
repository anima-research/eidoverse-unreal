// Isolated chat fixture; no credentials, avatars, production connection, or disk log.
const report={joins:0,says:0,whispers:0,leaves:0};
let seq=10;
let echo=true;
const server=Bun.serve({hostname:'127.0.0.1',port:18995,
 fetch(req,server){
  const u=new URL(req.url);
  if(u.pathname==='/ws'&&server.upgrade(req))return;
  if(u.pathname==='/report')return Response.json(report);
  if(u.pathname==='/echo/off'&&req.method==='POST'){echo=false;return Response.json({echo});}
  if(u.pathname==='/echo/on'&&req.method==='POST'){echo=true;return Response.json({echo});}
  return new Response('chat fixture');
 },
 websocket:{
  message(ws,data){
   const m=JSON.parse(String(data));
   const send=(x:any)=>ws.send(JSON.stringify(x));
   if(m.type==='join'){
    report.joins++;
    send({type:'snapshot',world:'water-local-test',you:'test-diver',throughSeq:seq,present:[]});
    const hello={type:'log',entry:{seq:++seq,actor:'fixture',verb:'say',args:{text:'Welcome underwater. Public chat is ready.'}}};
    send(hello);send(hello); // deduplicated by sequence
    send({type:'whisper',from:'fixture',text:'This is a private test message.'});
   }else if(m.type==='verb'&&m.verb==='say'){
    report.says++;if(echo)send({type:'log',entry:{seq:++seq,actor:'test-diver',verb:'say',args:{text:m.args.text}}});
   }else if(m.type==='whisper'){
    report.whispers++;send({type:'whisper',from:m.to,text:'Private reply received.'});
   }
  },close(){report.leaves++;}
 }});
console.log('CHAT_FIXTURE_READY ws://127.0.0.1:18995/ws');
process.on('SIGTERM',()=>{server.stop(true);process.exit(0);});

// A stand-in voice endpoint (browservoice.js synth protocol): hello, then each
// {type:"synth", id, text} gets a 330 Hz tone at 22050 Hz, 25 ms per character.
//   bun Tools/eidoverse/tts_bot.ts [port=8927] [seconds=120]
const port = Number(process.argv[2] ?? 8927), seconds = Number(process.argv[3] ?? 120);
const server = Bun.serve({
  port, hostname: "127.0.0.1",
  fetch(req, srv) { return srv.upgrade(req) ? undefined : new Response("tts bot", { status: 200 }); },
  websocket: {
    message(ws, data) {
      const m = JSON.parse(String(data));
      if (m.type === "hello") { console.log(JSON.stringify({ hello: m.name })); return; }
      if (m.type !== "synth") return;
      const rate = 22050, n = Math.round(rate * 0.025 * String(m.text).length);
      const pcm = new Int16Array(n);
      for (let i = 0; i < n; i++) pcm[i] = Math.round(Math.sin(2 * Math.PI * 330 * i / rate) * 0.5 * 32767);
      ws.send(JSON.stringify({ type: "synth-result", id: m.id, pcm: Buffer.from(pcm.buffer).toString("base64"), sampleRate: rate }));
      console.log(JSON.stringify({ synth: m.id, text: m.text, samples: n }));
    },
  },
});
setTimeout(() => { server.stop(true); process.exit(0); }, seconds * 1000);

// Conformance vectors for the native TTS chunker: the web's ttsChunks/spokenForm
// over a fixed set of utterances. bun Tools/eidoverse/tts_vectors.ts > Tools/eidoverse/tts_vectors.json
const repo = process.env.EIDOVERSE_WORLDS ?? `${process.env.HOME}/connectome-local/eidoverse-worlds-native`;
const { ttsChunks, spokenForm } = await import(`${repo}/client/lib/tts-chunk.js`);
const texts = [
  "hello there",
  "Hi! I'm here. Dr. Smith said hello to Mr. Jones at 6:04 today.",
  "Look at **this** `code` and _that_ — see https://example.com/path?x=1 for more 🌙✨",
  "Check [the docs](https://docs.example.com) and the world:staging setup -> it works => fine :: done | ok ~ yes",
  "```js\nconst x = 1;\n```\nThat was code, which should never be read aloud, and this sentence keeps going on and on so that it is long enough to be split at a soft boundary somewhere, because it is longer than ninety characters.",
  "A very long opening sentence that keeps going without any punctuation at all for quite a while until it finally stops somewhere far away.",
  "J. R. R. Tolkien wrote books. They are long. Very long indeed, and people read them anyway, because they love them, which is fine.",
  "Short. Bits. Here. And then a longer sentence follows that has some words.",
  "🌙",
  "Wait… really? Yes!  Okay then; let's go: now, and quickly, but carefully, so nobody trips over anything at all on the way there.",
];
console.log(JSON.stringify(texts.map((t) => ({ text: t, spoken: spokenForm(t), chunks: ttsChunks(t) })), null, 1));

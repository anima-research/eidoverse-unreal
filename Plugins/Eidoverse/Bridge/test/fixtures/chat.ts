// test/fixtures/chat.ts — the renderer's chat sends (eidoverse-unreal@59df129, Plugins/Eidoverse/Source/EidoverseClient/Private/), as sent today and in the protocol's form.

let n = 0;
const req = (src: string, verb: string, args: Record<string, unknown>) => ({ src, raw: JSON.stringify({ type: "request", id: `chat-${++n}`, verb, args }) });

/** Say, as Bridge.cpp:341 builds it ({type: verb, verb: say}), in request form. */
export const say = (text: string) => req("EidoverseBridge.cpp:341 (talk_bot.ts:17)", "say", { text });
/** A speech-to-text final (EidoverseVoiceGlue.cpp:226-229): `voiced` isn't a say argument, so the request form drops it. */
export const spoken = (text: string, utt: number) => req("EidoverseVoiceGlue.cpp:226-229", "say", { text: `\u{1F399} ${text}`, spoken: true, utt });
/** …and as sent today, with `voiced`: the authority refuses it (unknown-arg). */
export const spokenAsSent = (text: string, utt: number) => req("EidoverseVoiceGlue.cpp:227", "say", { text, voiced: true, spoken: true, utt });
/** A DM (EidoverseBridge.cpp:345-350): removed by policy; the server's door answers it. */
export const whisper = (to: string, text: string) => ({ src: "EidoverseBridge.cpp:345-350", raw: JSON.stringify({ type: "whisper", to, text }) });
/** The first earlier-chat page (EidoverseProtocol.cpp:330-338). */
export const historyPage = (reqId: string, before?: number) => ({ src: "EidoverseProtocol.cpp:330-338",
  raw: JSON.stringify({ type: "history", reqId, ...(before !== undefined ? { before } : {}), limit: 40, verbs: ["say"] }) });

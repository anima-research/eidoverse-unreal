I've written a full parity report for audio, voice and speech. It's in Markdown at the bottom of this message.

# Eidoverse audio, voice and speech: web client vs native Unreal client

Paths are shortened: `lib/…` = `~/connectome-local/eidoverse-worlds-native/client/lib`, `server/…`, `shared/…` and `notes/…` = the same repo, `UE/…` = `…/Plugins/Eidoverse/Source/EidoverseClient`.

## Is there any audio in the Unreal client today?

Confirmed: the Unreal client has no audio at all.
- **No audio modules:** `UE/EidoverseClient.Build.cs:11-16` links no audio, capture, voice or WebRTC modules (no AudioMixer, AudioCapture, Voice or WebRTC).
- **No audio code:** nothing in `Public/` or `Private/` creates a sound wave, audio component, capture device or peer connection.
- **Speech is text only:**
  - `say` log entries become chat lines plus a fake mouth envelope (`UE/Private/EidoverseBridge.cpp:584-586` → `EidoverseSeats.cpp:201-217`, which copies avatar.js's `1000 + 38 ms/char` timing).
  - The envelope drives VRM `aa` with `.5+.5·sin(17t)·sin(7.3t)` × .65 (`EidoverseAvatarLayers.cpp:249-251`).
  - That matches the web client's *fallback* only (`lib/avatar.js:2070-2073`). Real amplitude-driven mouths are missing.
- **Presence messages:** the client handles `typing` (always shows dots, ignores `state`; `EidoverseBridge.cpp:592-599`). It ignores `caption`, `performed`, `surface-transition`, `voice-*` and `sfu-*`. It sends `typing` without a `state` (`:137-143`).
- **Sound components:** placed `sound` components are not rendered.
- **macOS permissions:** only the engine's *editor* plist template has a microphone usage string (`UE_5.8/Engine/Build/Mac/Resources/Info-Editor.Template.plist:5`). The game template `Info.Template.plist` has none, and the project adds no plist or entitlements.

## How web audio works

```
                       ┌──────── world WebSocket /ws (JSON) ────────┐
 mic ─getUserMedia(AEC,NS)─▶ level gate (WebAudio) ─┐               │ relay-cred / voice-consent / sfu-* signalling
 own TTS ─Piper/endpoint─▶ 48k 20ms frames ─────────┼─▶ RTCPeerConnection ──Opus/48k RTP (DTLS-SRTP)──▶ werift SFU in the
                          (MediaStreamTrackGenerator)│   (1 per client)                          server process; forwards
                                                     │                                           ENCODED Opus, never decodes,
 per-speaker <audio> ◀─ one sendonly track per speaker (msid = speaker id) ◀── fan-out: consent ∧ ≤20 m ∧ not mod-muted
   └▶ volume = linear rolloff 3→20 m × voice volume × (not hushed)
   └▶ analyser peak → avatar 'aa' viseme + mic badge
 placed `sound` comp ─▶ <audio> → gain → HRTF panner (inverse distance) → world bus → speakers
```

- **One transport for everyone.** Humans and agents are both SFU speakers. An agent's MCPL "door" asks for a credential when its media sidecar attaches (`mcpl/net-server.ts:220-228`), and the sidecar speaks the same SFU signalling. Implement the SFU client once and you hear agents too.
- **No server-side TTS.** The server never makes or decodes audio. All TTS is on the speaker's side.
- **Feature flag:** voice exists only when the server runs with `VOICE_TRANSPORT=sfu` (`server/transport.ts:30-33`). Otherwise `relay-cred` answers `{type:"error", error:"no voice relay configured"}`.
- **No other sounds.** There are no UI sounds, ambient bed, rain or footsteps anywhere in the web client; the only non-voice audio is the `sound` component. The console probes `__audioState` and `whyIsItSilent` (`client/main.js:749,801`) reference `audioState` and `ambientDebug`, which no longer exist, so they throw.

---

## 1. Hearing other people and agents (SFU receive path)

**Unreal status:** missing.

**Web behaviour (spec)**

*Signalling* runs over the existing world socket, so no extra auth beyond the joined session cookie. Only the embodied primary may use it: not a spectator, `surface` must be `"world"` (`server/server.ts:843-1050`).

1. After joining, the client sends `{type:"relay-cred"}`, at most once per join (`lib/voicesfubridge.js:63-79`, `lib/net.js:179-181`). It also sends `{type:"voice-consent", recv:<bool>}` right away and on every change (`voicesfubridge.js:171-172`).
2. The server reply (`server/server.ts:967-1003`, `server/sfuadapter.ts:120-166`):
   `{type:"relay-cred", nonce, incarnation, identity:"<id>#g<mediaGen>", world, id, primaryGen, mediaGen, transport:"sfu", iceServers, gen:mediaGen, service:{enabled:true, …, transport:"sfu"}}`.
   - `iceServers` comes from the `SFU_ICE_SERVERS` env JSON, defaulting to `[{urls:"stun:stun.l.google.com:19302"}]` (`sfuadapter.ts:191-206`).
   - Other clients receive `{type:"surface-transition", id, surface:"voice-relay", gen:mediaGen, retired:null}`.
3. The client builds `RTCPeerConnection({iceServers: cred.iceServers})` (`lib/voicesfu.js:119`) and sends `{type:"sfu-want-negotiate"}` (`voicesfubridge.js:161`).
4. **The server always offers.** The client only ever answers (`voicesfu.js:15-19`).
   - Before each offer the server sends `{type:"sfu-route", speaker}` for every route.
   - Then `{type:"sfu-offer", sdp}` (`sfuadapter.ts:437-452`).
   - The SDP has Opus 48 kHz, 2 channels, payload type 111, `useinbandfec=1` (`server/sfu.ts:37-39`, `sfuadapter.ts:215-224`), with `max-bundle` and no IPv6 (`sfu.ts:242-246`).
   - Server ICE candidates are inside the SDP. The server does not trickle.
5. The client sets the remote description, applies any queued ICE, then answers (`voicesfu.js:240-291`):
   `{type:"sfu-answer", sdp, gen:mediaGen, cred:{world, id, primaryGen, mediaGen, incarnation, nonce}}`.
   - The first answer is checked against seven admission refusals: world, id, live primary gen, live media gen, incarnation, and the nonce must not be reused (`server/relaydecision.ts:41-51`). A refusal sends `{type:"error", error:"voice leg refused: …"}` and revokes the leg.
   - Later answers skip the credential check but still need the right `gen` (`server.ts:870-875`, `sfuadapter.ts:478-510`).
   - The server waits 15 s for an answer (`sfuadapter.ts:465`).
6. The client trickles `{type:"sfu-ice", candidate:<RTCIceCandidate JSON>, gen:mediaGen}` (`voicesfu.js:158-165`). The server drops candidates with a missing or stale gen.
7. **Mapping tracks to speakers.** Each inbound track's stream id (msid) is the speaker's id (`sfu.ts:401`). Use the msid, falling back to the `sfu-route` arrival order (`voicesfu.js:133-151`). Each track becomes one playback sink per speaker.
8. **Speakers coming and going** (`voicesfubridge.js:99-113`):
   - `surface-transition` with `surface ∈ {voice, voice-relay}` and `gen == null` → drop that speaker.
   - With a new gen → send `sfu-want-negotiate`.
   - The server also starts renegotiations on its own whenever routes change.
9. **What the server forwards** (`sfu.ts:321-343`). It forwards to a listener only if all of these hold:
   - the listener consented (`voice-consent recv:true`; absent means denied);
   - the speaker is not moderator-muted;
   - the speaker is in earshot: enter at 20 m, leave at 22 m (×1.10 hysteresis), and it forwards anyway if a position is unknown or older than 5 s (`sfu.ts:123-171`);
   - the per-listener cap, which is off (`maxAudiblePerListener=0`).

**Native approach**
- **Recommended: UE's bundled libwebrtc.** `Engine/Source/ThirdParty/WebRTC/5414`. A Mac `Lib/Mac/Release/libwebrtc.a` is present; PixelStreaming2 uses it on Mac.
  - Add a `WebRTC` dependency, create a `PeerConnectionFactory` with a **custom AudioDeviceModule**, as PixelStreaming2 does.
  - Attach an `AudioTrackSinkInterface` to each remote track. That gives decoded 10 ms PCM frames (16-bit, usually 48 kHz).
  - Push each speaker's PCM into a `USoundWaveProcedural` on a `UAudioComponent` attached to that peer's avatar.
  - This keeps NetEq (jitter buffer, packet-loss concealment) and FEC, which is the whole reason the server design works.
- **Alternative: libdatachannel plus UE's libOpus.** Lighter to build, but you would have to write a jitter buffer and loss concealment yourself. The SFU spec notes that cost Basis ~750 lines (`notes/SFU-SPEC.md`). Not recommended.
- **Plan B, needs server work:** carry encoded RTP over the WebSocket (binary frames). The server still would not decode, but it needs a new leg type, and the client still needs a jitter buffer.
- **Never mix tracks.** Keep one sink per speaker; mouths and the volume rolloff depend on it.

**Effort:** L

**Notes**
- **Risk: the playout pump.** With a custom ADM, libwebrtc only decodes remote audio while something pulls playout. Run a 10 ms thread that calls `AudioTransport::NeedMorePlayData` and discards the mixed result (PixelStreaming2 does this). Without it the per-track sinks never fire.
- **Connection diagnostics** (`voicesfu.js:192-235`, `voicesfubridge.js:127-144`):
  - If ICE stays in `checking` for 8 s, or reaches `failed`, show a chat line and recover.
  - Recovery re-sends `relay-cred` plus `voice-consent` after 2 s, 8 s, then 20 s; give up after 3 tries. A successful connection resets the count.
- **Server state broadcast:** `{type:"voice-service", state, incarnation, reason}` (`server.ts:80-84`). A changed incarnation means every credential is stale and a fresh `relay-cred` is needed.
- **Moderator mute:** `{type:"voice-moderated", id, muted, by}` (`server.ts:1033`).
- **Reconnect:** close the previous peer connection before creating a new one (`voicesfu.js:100-103`).

## 2. Voice volume by distance, hush, and voice volume

**Unreal status:** missing.

**Web behaviour (spec)** (`voicesfubridge.js:183-200`)
- Every 300 ms: `d` = distance from my position to the speaker's avatar root, in metres. `roll = clamp(1 - (d-3)/(20-3), 0, 1)`.
- Target volume = `hushed ? 0 : roll × volVoices`.
- Every 60 ms, step the actual volume toward the target by 60/700 ≈ 0.086, so a full swing takes about 0.7 s.
- **Voices are not panned.** Plain `<audio>` elements, no HRTF.
- **Sending my position:** every 500 ms while connected, send `{type:"sfu-pos", x, y, z}` in metres, 2 decimals, in the world frame (`voicesfubridge.js:205-209`). This only feeds the server's earshot gate.
- **Hush** is local volume 0 while the stream keeps flowing. It is different from consent (section 5).

**Native approach**
- Same constants. Set `UAudioComponent::SetVolumeMultiplier` per speaker on the same 300 ms and 60 ms cadence.
- Play voices non-spatialized (2D) for exact parity. Optionally offer spatialization as an extra.
- Convert positions with the existing `ToWirePosition` (`EidoverseBridge.cpp`).

**Effort:** S

**Notes:** the server ignores `sfu-pos` from spectators and aux legs, to stop anyone forging another person's position as a remote mute (`server.ts:918-938`).

## 3. Mouth movement and the "speaking" glyph driven by real audio

**Unreal status:** partial. Only the text-based fake envelope exists.

**Web behaviour (spec)**
- **Level measurement:** for each speaker, take the peak `|x|` over the last 512 samples (`voicesfu.js:662-680`). My own level is the same peak, measured on the *raw* mic before the gate (`:634-659`).
- **Each frame** (`lib/voicemouths.js:53-85`):
  - Others: `avatar.voiceLevel = level`. If level > `VOICE_GATE` (0.045), `noteSpeaking(id, 600 ms)`.
  - Me: `voiceLevel` is set only while the mic is on and not muted. Speaking = level > 0.045. Then `speakUntil = now + 400`, and the 🎙 glyph shows with a 900 ms tail.
  - The glyph is broadcast as `{type:"typing", state:"mic"}`, or `typing` with no state to clear it, on each change and refreshed at most every 1.5 s.
- **Viseme** (`avatar.js:2065-2069`): `target = min(1, level × 3.2)`. Each frame, `mouth += (target - mouth) × (rising ? 0.55 : 0.18)`. Set VRM `aa = mouth × 0.8`. If there is no level, fall back to the envelope.
- **HUD mic icon** turns "hot" (gold) when level > 0.02 and, in push-to-talk mode, the key is held. Checked every 125 ms (`lib/mictoggle.js:188-197`).

**Native approach**
- Compute the peak in the libwebrtc sink callback, or on the procedural wave's submitted buffers, for each speaker. Expose it as `float VoiceLevel` on the peer.
- In `EidoverseAvatarLayers.cpp:249-251`, prefer the real level and keep the envelope as the fallback.
- Handle incoming `typing` with `state:"mic"` (also `ear`, `think`, `tool`) as a glyph instead of typing dots.

**Effort:** S, once section 1 exists.

**Notes:** you can show the incoming `state` glyphs now, with no audio work.

## 4. Mic capture and publishing (voice activation, push-to-talk, mute)

**Unreal status:** missing.

**Web behaviour (spec)**

*Capture* is `getUserMedia({audio:{echoCancellation:true, noiseSuppression:true}})` (`lib/voicesource.js:96-98`). The browser also applies auto gain.

*Picking the transceiver to publish on* (`voicesfu.js:566-582`). This is subtle and essential:
1. Reuse the transceiver whose `currentDirection == 'sendonly'` with `replaceTrack`.
2. Otherwise use the `inactive` "floor" m-line the server offered for my mic: `replaceTrack`, then set direction to `sendonly`.
3. Use `addTrack` only when neither exists.

Then send `sfu-want-negotiate`. After each answer, if a live mic track is stuck on a non-sending m-line, move it to the floor (`:268-280`). Only one microphone acquisition may run at a time (`:333-339`).

*Voice-activation gate* (`lib/micgate.js`, `lib/micstate.js`):
- Signal path: source → 50 ms lookahead delay → gain (starts at 0) → sent stream.
- Gain opens with time constant τ = 8 ms and closes with τ = 120 ms (`micgate.js:59-71, 154-170`).
- Every 20 ms, measure RMS over 512 samples (`:363-385, 471`).
- Threshold: `dB = -60 + (micFloor/0.2) × 60`. Default `micFloor` is 0.12 → -24 dBFS → amplitude ≈ 0.063 (`lib/voiceconsent.js:55, 218-221`).
- Close only below 0.7 × threshold (about 3 dB of hysteresis) and after a 700 ms hang time. The first 8 ticks only measure; the gate stays closed (`micgate.js:432-458`).
- On onset (open ≥ 60 ms, at most once per 1.5 s), send `typing state:"mic"`.
- If the gate cannot be built, refuse to transmit unless the user explicitly opts into "transmit ungated" (`voicesfu.js:487-494`).

*Push-to-talk* (`prefs.pttMode`, default off): the gate opens only while V is held. Releasing V, or the window losing focus, closes it (`mictoggle.js:369-395`, `micstate.js:73-103`).

*V key:* if not typing in a text field and Shift is not held, V toggles the mic, or acts as push-to-talk in that mode.

*Mute* is separate from mic on/off and closes the gate at once (`micstate.js:52-57`).

*Self monitor* plays back the gated stream with a 0.4 s delay at gain 0.35, headphones advised (`micgate.js:205-251`).

**Native approach**
- **Recommended: macOS voice processing for capture.** Use `AVAudioEngine.inputNode` with `setVoiceProcessingEnabled(true)` (the VPIO audio unit). It gives OS-level echo cancellation, noise suppression and auto gain that also cancel UE's own audio output.
- Run the same RMS gate in C++ on 10 ms blocks at 48 kHz mono, then push into libwebrtc through the custom ADM's recording callback, or a custom `AudioSourceInterface`.
- **Alternatives:** UE's `AudioCapture` plugin (`Audio::FAudioCapture`) or the `Voice` module's `IVoiceCapture` (Mac implementation in `Runtime/Online/Voice/Private/Mac`). Both are fine, but neither gives echo cancellation. In that case enable libwebrtc's audio processing, but its echo reference will not match the volume-shaped UE mix. Otherwise tell users to wear headphones.
- **Opus encoding:** libwebrtc does it (48 kHz, 20 ms frames, in-band FEC).

**Effort:** M, on top of section 1.

**Notes**
- **macOS permission and plist risk:**
  - The packaged game needs `NSMicrophoneUsageDescription`, added through the project's Mac plist settings.
  - With the hardened runtime it also needs the `com.apple.security.device.audio-input` entitlement.
  - Call `AVCaptureDevice requestAccessForMediaType:AVMediaTypeAudio` before capturing. Without permission, macOS delivers silent buffers rather than an error.
  - In the editor, the permission belongs to UnrealEditor.app.
- Turning the mic off must stop the device, so the hardware light goes off (`voicesfu.js:83-90`).
- **Mic beats TTS:** a live mic always wins over TTS; with the mic off, the TTS source is published instead (section 8).

## 5. Consent, HUD toggles and the audio settings panel

**Unreal status:** missing.

**Web behaviour (spec)**
- **Saved prefs:** localStorage `eido.audio.prefs` = `{recvVoice:false, volVoices:1, volWorld:1, volTts:1, sttConsent:null, micFloor:0.12, pttMode:false}` (`voiceconsent.js:20-60`).
- **Voice is off by default in both directions.**
  - Receive consent (`recvVoice`) is enforced on the server. While it is off, no route exists at all ("unplugs the wire").
  - The server acknowledges with `{type:"voice-consent", recv, applied, note?}`. Consent is tied to the session's generation and starts closed on reconnect (`server.ts:1038-1050`, `sfuadapter.ts:265-323`).
- **HUD buttons** beside the HUD (`mictoggle.js`):
  - **Mic:** off / on / hot.
  - **Ear:** first click grants receive consent; after that it toggles hush.
- **Panel rows** (`lib/audiopanel.js:330-470`):
  - mic toggle
  - push-to-talk
  - mic sensitivity: slider over a live dB meter spanning -60..0 dB
  - hear voices
  - voice volume
  - world volume
  - TTS section and self-TTS volume
  - hear my own mic (monitor)
  - transmit ungated (shown only if the gate failed)
  - speech-to-text consent
  - "connect to other people's audio" (receive consent)
- **STT consent** is tri-state: null = never asked, true, false. A confirm dialog explains that audio goes to a cloud recognizer (`voiceconsent.js:146-176`).

**Native approach**
- Mirror these prefs in `UEidoverseSettings` or a SaveGame.
- Add a UMG audio section to the existing settings UI (`EidoverseUI*.cpp`) and two HUD icons.
- Send `voice-consent` on join and on every change.

**Effort:** S–M

**Notes:** keep the defaults off; this is a privacy rule, not a style choice. Moderator mute: `{type:"voice-moderate", id, muted}` from an owner (rank 2) mutes a speaker for everyone, applied at the source (`server.ts:1005-1037`). The web client has no UI for this; it is console-only.

## 6. Placed world sounds (the `sound` component)

**Unreal status:** missing.

**Web behaviour (spec)** (`lib/sounds.js`, `shared/sound.js`)
- **Source:** `comp {id, type:"sound", data:{src, playing=true, loop=true, volume=0.8 (0..1), radius=12 (1..200), t0?, look?}}`. `data:null` removes it. Entity removal or world reset tears it down.
- **Allowed `src`:** a path under `eidoverse/assets/` or `store/audio/`, ending in `.mp3/.ogg/.opus/.wav/.webm/.m4a`. No URLs or `..`. Fetched from `GET {http}/library/{src}` (`shared/sound.js:30-52`).
- **Audio graph** (`sounds.js:68-86`): element → gain(`volume`) → panner (HRTF, `distanceModel:'inverse'`, `refDistance` 1.5, `rolloffFactor` 1.5, `maxDistance` = radius) → world bus (gain = `volWorld`) → output.
  - With WebAudio's inverse model, `maxDistance` does **not** cut the sound off. Gain = `1.5/(1.5 + 1.5·(max(d,1.5) - 1.5))`, so the default radius only reduces it to about 8.7 % at 12 m.
- **Every 2nd frame** (`client/main.js:529`, `sounds.js:133-152`): the listener follows the camera (position, forward, up) and each panner follows its entity's world position. If the entity is gone, gain is 0.
- **Shared playhead:** if `t0` (epoch ms) is set, seek to `(now - t0)/1000`, wrapped modulo the duration when looping (`sounds.js:98-109`).
- **Authoring:** upload with `POST /upload?as=audio&name=<file>[&token=]` → `{path}` under `store/audio/` (`:163-171`). Editing only commits the `comp` verb.

**Native approach**
- **Decoding:**
  - mp3/m4a/wav: Apple AudioToolbox (`ExtAudioFile`).
  - ogg/opus: UE's bundled Ogg/Vorbis and libOpus.
  - Or adopt the third-party RuntimeAudioImporter plugin, which handles mp3/wav/flac/ogg/opus.
  - .webm (Matroska) has no easy decoder; log and skip.
- Build a `USoundWaveProcedural`, or a runtime `USoundWave` with PCM, and play it on a `UAudioComponent` attached to the entity.
- Use a `USoundAttenuation` with HRTF spatialization and a custom attenuation curve that reproduces the formula above. UE's built-in "Inverse" falloff is a different curve.
- Route through a "World" sound class or submix whose volume = `volWorld`.
- Apply the `t0` seek with `Play(StartTime)`.

**Effort:** M

**Notes:** this is the only placed or ambient audio the web client has. There are no rain, footstep, ambient or UI sounds to port.

## 7. Live speech captions (bubbles and timing)

**Unreal status:** missing; only the logged `say` is handled.

**Web behaviour (spec)**
- **Live captions:**
  - Server → client: `{type:"caption", id, text≤500, utt}`. Presence only, never logged (`server/messages.ts:375-399`).
  - The web client shows a bubble and mouth via `av.say(text)`, plus `noteSpeaking(id, min(12000, 3000 + len×30))` (`voicemouths.js:95-103`). The finished utterance arrives later as one `say` with `spoken:true, utt, t0`, and chat merges lines with the same `utt` (`lib/chat.js:217-253`).
- **Holding for voiced speakers** (`lib/realize/causes.js:27-117`):
  - Who has a voice: actors with a live `surface:"voice"` leg, from `snapshot.present[].surfaces` and `surface-transition`.
  - For their `say`, the bubble waits up to 2500 ms for `{type:"performed", id, seq, gen, rung, incarnation}` before showing.
  - If a matching `performed` arrives, the bubble shows then (or not at all if the say was already caption-paced). Otherwise it shows after 2500 ms.

**Native approach**
- Handle `caption` in `EidoverseBridge.cpp` next to `typing`: show the nameplate bubble, call `NoteSpeaker` with the formula above, and merge chat lines by `utt`.
- Optionally add the `performed` hold.

**Effort:** S

**Notes:** no audio needed; worth doing early. `client/captions.html` is a separate overlay for OBS streams that talks to the external Portal service (`wss://portal.animalabs.ai`, protocol v4). It is not part of the world client and needs no native port. The `caption` *verb* (`shared/captions.js`, which feeds `comp.captions` for screen entities) is not drawn by the web client either.

## 8. Speaking with your own synthesized voice (bring-your-own-voice TTS)

**Unreal status:** missing.

**Web behaviour (spec)**

*Voice sources.* Each is a function `(text) → {pcm: Int16 mono, sampleRate}` (`lib/voiceengines.js`):
- **Piper in the browser** (`lib/engine-piper.js`): a local `.onnx` model plus `<name>.onnx.json`.
  - Text → espeak-ng phoneme ids in a worker (`lib/phon.worker.js`, piper_phonemize wasm), then onnxruntime-web (WebGPU or wasm).
  - Inputs `input` (int64), `input_lengths`, `scales = [noise_scale 0.667, length_scale 1.0, noise_w 0.8]` (taken from the model's `inference` config), output float → s16. The sample rate comes from the config's `audio.sample_rate`, typically 22050.
- **Endpoint over WebSocket** (`lib/browservoice.js:29-61`, `lib/piperbridge.js`; `?tts=<port>` defaults to `ws://127.0.0.1:8927`, which first sends `{type:"hello", name:"voicesource", world:""}`):
  - Send `{type:"synth", id, text}`; receive `{type:"synth-result", id, pcm:<base64 s16le>, sampleRate}`.
  - 4 s connect timeout, 15 s per request.
- **Endpoint over HTTP:** `POST {text}` → WAV or MP3 bytes.

*Text chunking* (`lib/tts-chunk.js`):
- Clean up text first: no code blocks, links become their label, bare URLs become "link", no emoji.
- Split at sentence boundaries, skipping abbreviations.
- Join chunks shorter than 20 chars; split chunks longer than 90 at soft breaks, keeping at least 35.
- Make the first chunk 20–64 chars so it synthesizes fast.
- Each speaker's chunks play in order.

*Playback and publishing* (`lib/tts.js:337-607`):
- Resample to 48 kHz and feed a paced queue of 20 ms (960-sample) frames into a `MediaStreamTrackGenerator` (Chromium-only API). That track is published on the SFU **in place of the mic when the mic is off**.
- Sidetone (hearing your own TTS) is on by default at gain `0.8 × volTts`.
- Only your own `say` gets spoken: `speakOwnSays`, which skips if TTS is off, the mic is live, or the say was already `performed` (`tts.js:640-668`).
- The panel keeps a list of voices: IndexedDB file handles plus a SHA-256 identity (`lib/voicestore.js`, `lib/ttsrow.js`).

**Native approach**
- **Phase 1:** implement the WebSocket synth protocol and the HTTP endpoint natively (`IWebSocket` / FHttpModule, base64 → int16, resample to 48 kHz). Feed frames into the same libwebrtc audio source used for the mic, swapping sources when the mic is off. Play sidetone through a procedural wave.
- **Phase 2, optional:** local Piper through UE's NNE with the ONNX Runtime CPU backend, plus a native espeak-ng phonemizer.
- **Not recommended:** UE's experimental `TextToSpeech` plugin (macOS uses AVSpeechSynthesizer). It plays straight to the speakers and cannot be routed into WebRTC, the same reason the web client rejects browser `speechSynthesis` (`browservoice.js:17-21`). `AVSpeechSynthesizer.write(_:toBufferCallback:)` could produce buffers, but that would be custom Objective-C++.

**Effort:** M for endpoints; L for local Piper.

**Notes:**
- **Licensing risk:** espeak-ng is GPL-3.
- TTS is off by default.
- Agents don't need this on the native client; their voice is already an SFU speaker.

## 9. Speech-to-text

**Unreal status:** missing.

**Web behaviour (spec)** (`lib/stt.js:124-327`)
- **Engine:** the browser's Web Speech API (Google's cloud in Chrome).
- **Settings:** `continuous=true`, `interimResults=false`, `lang=navigator.language`.
- **When it runs:** only while STT consent is true, the mic is live, and the API exists. It restarts automatically on `onend` while still wanted.
- **Output:** each final result is sent as `{type:"verb", verb:"say", args:{text:"🎙 "+text, voiced:true, spoken:true, utt:++n}}`.
- **Errors** become chat hints: `audio-capture`, `not-allowed`, `network`, and so on. A "no audio after 4 s" check flags mic contention.

**Native approach**
- macOS `SFSpeechRecognizer` with `SFSpeechAudioBufferRecognitionRequest`, fed from the *same* captured buffers. Set `requiresOnDeviceRecognition=true` where supported.
- Needs `NSSpeechRecognitionUsageDescription` plus the mic permission.
- Alternative: whisper.cpp running locally.
- Send the same `say` shape.

**Effort:** M

**Notes:** keep within the server's rate limit of 12 authored verbs per 4 s. Unlike the browser, the native client can share one capture between voice and STT; the browser's recognizer opens its own mic.

## 10. Browser-only pieces: no native port needed

- `lib/audioctx.js` (one AudioContext, Chrome's ~6-context cap) and `lib/audiounlock.js` (autoplay unlock on a user gesture): the UE audio mixer has neither problem.
- `lib/voicestore.js`: IndexedDB file handles. Store plain file paths in settings instead.
- `lib/engines.js` and `lib/voiceengines.js`: an engine registry. A small C++ interface is enough.
- `lib/wavpcm.js`: a 12-line WAV parser. Port it trivially if HTTP endpoints return WAV.
- `lib/headset_seen.js` is about XR headsets, not audio.
- Diagnostics (`relayDiag`, `sfuStats` via `getStats()`): replicate with libwebrtc stats only for debugging.

---

## Server cooperation needed

- **None** for sections 1–9 with libwebrtc. All signalling already exists on the world socket, and the native client joins as an ordinary embodied primary with the `ew_sess` cookie (`EidoverseBridge.cpp:201`).
- **Server configuration** is needed for voice to exist at all:
  - `VOICE_TRANSPORT=sfu` on the server.
  - A reachable public address for the SFU.
  - `SFU_ICE_SERVERS` (STUN/TURN) for clients on other networks; the default is Google STUN only, with no TURN.
- **Server changes** would only be needed for a non-WebRTC route, such as RTP over the WebSocket.

## Recommended order

1. Captions and `typing` state glyphs (S, no audio).
2. Receive voices: libwebrtc plus per-speaker procedural waves, consent, settings (L).
3. Distance volume, hush, real-level mouths (S).
4. Publish the mic with voice processing, the gate, push-to-talk, and the macOS permission plist (M).
5. Placed `sound` components (M).
6. TTS endpoints (M).
7. STT (M).
"""Assemble independently replaceable MRQ shots into a captioned H.264 reel.
Usage: python3 assemble_reel.py --frames /path/final --output /path/output
Retains clean per-shot clips and a clean master, plus ASS/SRT caption sources.
"""
import argparse,json,pathlib,subprocess
HERE=pathlib.Path(__file__).resolve().parent
def run(args):subprocess.run(args,check=True)
def ass_time(t):
 h=int(t//3600);m=int(t%3600//60);s=t%60
 return f'{h}:{m:02d}:{s:05.2f}'
def srt_time(t):return f'{int(t//3600):02d}:{int(t%3600//60):02d}:{int(t%60):02d},{round((t%1)*1000):03d}'
def main():
 p=argparse.ArgumentParser();p.add_argument('--frames',type=pathlib.Path,required=True);p.add_argument('--output',type=pathlib.Path,required=True);p.add_argument('--force',action='store_true');p.add_argument('--edition',type=int,default=1);args=p.parse_args()
 edition=f'{args.edition:02d}'
 recipe=json.loads((HERE/'shots.json').read_text());fps=recipe['fps'];out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
 clips=out/'clips';clips.mkdir(exist_ok=True)
 ass=['[Script Info]','ScriptType: v4.00+','PlayResX: 1920','PlayResY: 1080','ScaledBorderAndShadow: yes','','[V4+ Styles]',
 'Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding',
 'Style: Title,Helvetica Neue,46,&H00FFFFFF,&H00FFFFFF,&H60080503,&H90080503,-1,0,0,0,100,100,0,0,1,0.8,0,7,72,72,48,1',
 'Style: Body,Helvetica Neue,28,&H00EAE2D8,&H00EAE2D8,&H60080503,&H90080503,0,0,0,0,100,100,0,0,1,0.7,0,7,72,72,48,1',
 'Style: Label,Helvetica Neue,22,&H00CAD8A0,&H00CAD8A0,&H60080503,&H90080503,0,0,0,0,100,100,2,0,1,0.5,0,7,72,72,48,1',
 '', '[Events]', 'Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text']
 srt=[];timeline=[];time=0
 for index,shot in enumerate(recipe['shots']):
  frames=sorted((args.frames/shot['id']).glob('*.png'));expected=round(shot['seconds']*fps)
  if len(frames)!=expected:raise RuntimeError(f"{shot['id']}: expected {expected} frames, found {len(frames)}")
  numbers=[int(x.stem) for x in frames]
  if numbers!=list(range(numbers[0],numbers[0]+expected)):raise RuntimeError('Non-contiguous frames: '+shot['id'])
  clip=clips/(shot['id']+'.mp4')
  if args.force or not clip.exists():
   run(['ffmpeg','-hide_banner','-loglevel','warning','-y' if args.force else '-n','-framerate',str(fps),'-start_number',str(numbers[0]),'-i',str(frames[0].parent/'%05d.png'),'-frames:v',str(expected),'-c:v','libx264','-preset','slow','-crf','16','-pix_fmt','yuv420p','-color_primaries','bt709','-color_trc','bt709','-colorspace','bt709','-movflags','+faststart',str(clip)])
  start=time;end=time+shot['seconds'];timeline.append({'id':shot['id'],'start':start,'end':end,'file':str(clip),'title':shot['title'],'caption':shot['caption']})
  a=ass_time(start+.35);b=ass_time(end-.2)
  label=f"UNDERWATER  /  DEMO REEL {edition}                                      {index+1:02d} / {len(recipe['shots']):02d}"
  ass.append(f'Dialogue: 1,{a},{b},Label,,0,0,0,,{{\\pos(72,72)\\fad(220,180)}}{label}')
  ass.append(f'Dialogue: 1,{a},{b},Title,,0,0,0,,{{\\pos(72,893)\\fad(220,180)}}{shot["title"]}')
  ass.append(f'Dialogue: 1,{a},{b},Body,,0,0,0,,{{\\pos(72,957)\\fad(220,180)}}{shot["caption"]}')
  srt.extend([str(index+1),srt_time(start+.35)+' --> '+srt_time(end-.2),shot['title']+'\n'+shot['caption'],''])
  time=end
 (out/'captions.ass').write_text('\n'.join(ass)+'\n');(out/'captions.srt').write_text('\n'.join(srt));(out/'timeline.json').write_text(json.dumps(timeline,indent=2))
 concat=out/'clips.ffconcat';concat.write_text('ffconcat version 1.0\n'+'\n'.join("file '"+str(pathlib.Path(s['file'])).replace("'","'\\''")+"'" for s in timeline)+'\n')
 clean=out/f'Underwater_Demo_Reel_{edition}_clean.mp4'
 run(['ffmpeg','-hide_banner','-loglevel','warning','-y' if args.force else '-n','-f','concat','-safe','0','-i',str(concat),'-c','copy','-movflags','+faststart',str(clean)])
 final=out/f'Underwater_Demo_Reel_{edition}.mp4'
 # Understated translucent caption backing; top and bottom picture remain visible.
 vf="drawbox=x=0:y=862:w=iw:h=218:color=0x03131C@0.62:t=fill,drawbox=x=0:y=0:w=iw:h=124:color=0x03131C@0.24:t=fill,ass="+str(out/'captions.ass').replace(':','\\:')+",fade=t=in:st=0:d=0.45,fade=t=out:st="+str(time-.6)+":d=0.6"
 run(['ffmpeg','-hide_banner','-loglevel','warning','-y' if args.force else '-n','-i',str(clean),'-vf',vf,'-c:v','libx264','-preset','slow','-crf','17','-pix_fmt','yuv420p','-color_primaries','bt709','-color_trc','bt709','-colorspace','bt709','-movflags','+faststart','-metadata',f'title=Underwater — Demo Reel {edition}','-metadata','comment=In-engine cinematic feature tour. No added audio. Editable shots and captions retained.',str(final)])
 print(json.dumps({'video':str(final),'duration':time,'shots':len(timeline),'fps':fps},indent=2))
if __name__=='__main__':main()

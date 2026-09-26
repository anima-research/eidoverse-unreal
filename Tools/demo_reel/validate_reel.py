"""Validate frame continuity, picture changes, final encode, and make QA sheets."""
import argparse,json,pathlib,subprocess
HERE=pathlib.Path(__file__).resolve().parent
def main():
 p=argparse.ArgumentParser();p.add_argument('--frames',type=pathlib.Path,required=True);p.add_argument('--output',type=pathlib.Path,required=True);p.add_argument('--edition',type=int,default=1);args=p.parse_args()
 recipe=json.loads((HERE/'shots.json').read_text());args.output.mkdir(parents=True,exist_ok=True)
 report={'shots':[]};sheet_inputs=[];sheet_filters=[]
 for index,shot in enumerate(recipe['shots']):
  frames=sorted((args.frames/shot['id']).glob('*.png'));expected=round(shot['seconds']*recipe['fps'])
  assert len(frames)==expected,(shot['id'],len(frames),expected)
  numbers=[int(f.stem) for f in frames];assert numbers==list(range(expected)),shot['id']
  def pixels(path):return subprocess.run(['ffmpeg','-v','error','-i',str(path),'-vf','scale=160:90','-f','rawvideo','-pix_fmt','rgb24','-'],capture_output=True,check=True).stdout
  first=pixels(frames[0]);last=pixels(frames[-1])
  assert len(first)==len(last)==160*90*3
  delta=sum(abs(a-b) for a,b in zip(first,last))/len(first)
  assert delta>.2,(shot['id'],'apparently static')
  sheet_inputs.extend(['-i',str(frames[len(frames)//2])])
  sheet_filters.append(f"[{index}:v]scale=480:270,pad=480:300:0:0:color=0x061923,drawtext=text='{shot['id']} / {shot['seconds']}s':fontsize=18:fontcolor=white:x=12:y=279[v{index}]")
  report['shots'].append({'id':shot['id'],'frames':expected,'first_last_mean_difference':round(delta,2)})
 layout='|'.join(f'{i%3*480}_{i//3*300}' for i in range(len(recipe['shots'])))
 sheet_filters.append(''.join(f'[v{i}]' for i in range(len(recipe['shots'])))+f'xstack=inputs={len(recipe["shots"])}:layout={layout}[sheet]')
 subprocess.run(['ffmpeg','-hide_banner','-v','error','-y',*sheet_inputs,'-filter_complex',';'.join(sheet_filters),'-map','[sheet]','-frames:v','1',str(args.output/'shot_contact_sheet.jpg')],check=True)
 video=args.output/f'Underwater_Demo_Reel_{args.edition:02d}.mp4'
 if video.exists():
  result=subprocess.run(['ffprobe','-v','error','-show_streams','-show_format','-of','json',str(video)],capture_output=True,text=True,check=True)
  data=json.loads(result.stdout);stream=next(s for s in data['streams'] if s['codec_type']=='video')
  total=sum(s['seconds'] for s in recipe['shots'])
  assert stream['width']==1920 and stream['height']==1080 and stream['r_frame_rate']=='30/1'
  assert abs(float(data['format']['duration'])-total)<.05,data['format']['duration']
  assert int(stream['nb_frames'])==total*recipe['fps']
  subprocess.run(['ffmpeg','-hide_banner','-v','error','-i',str(video),'-f','null','-'],check=True)
  report['video']={'file':str(video),'seconds':total,'frames':stream['nb_frames'],'codec':stream['codec_name'],'bytes':data['format']['size']}
 (args.output/'validation.json').write_text(json.dumps(report,indent=2))
 print(json.dumps(report,indent=2))
if __name__=='__main__':main()

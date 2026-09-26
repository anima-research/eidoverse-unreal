"""Versioned 26 s prototype review with source clips and readable captions."""
import json,subprocess
from pathlib import Path
here=Path(__file__).resolve().parent
root=Path('/Users/olena/unrealtest/demo_reel/spectral_film_02')
out=root/'output';out.mkdir(exist_ok=True)
recipe=json.loads((here/'shots_spectral.json').read_text())
clips=[];timeline=[];start=0
review_titles={
    '07_surface':'Submarine surfacing / local wake and foam',
    '09_platform':'Multi-scale waves / physical water shading',
    '10_underwater':'Underwater refraction / critical-angle reflection',
    '11_wave_detail':'Displaced wave geometry / waterline view',
}
for shot in recipe['shots']:
    frames=sorted((root/shot['id']).glob('*.png'));expected=round(shot['seconds']*recipe['fps'])
    assert [int(f.stem) for f in frames]==list(range(expected)),(shot['id'],len(frames),expected)
    clip=out/(shot['id']+'.mp4')
    if not clip.exists():subprocess.run(['ffmpeg','-hide_banner','-v','error','-n','-framerate','30','-i',str(root/shot['id']/'%05d.png'),'-c:v','libx264','-preset','slow','-crf','16','-pix_fmt','yuv420p','-movflags','+faststart',str(clip)],check=True)
    clips.append(clip);timeline.append(dict(shot=shot['id'],start=start,end=start+shot['seconds'],title=review_titles[shot['id']]));start+=shot['seconds']
concat=out/'clips.ffconcat';concat.write_text('ffconcat version 1.0\n'+''.join("file '"+str(c)+"'\n" for c in clips))
clean=out/'Spectral_Ocean_Prototype_clean.mp4'
subprocess.run(['ffmpeg','-hide_banner','-v','error','-n','-f','concat','-safe','0','-i',str(concat),'-c','copy','-movflags','+faststart',str(clean)],check=True)
filters=['drawbox=x=0:y=955:w=iw:h=125:color=0x03131c@.65:t=fill']
for item in timeline:
    title=item['title'].replace("'",'')
    filters.append(f"drawtext=text='{title}':font='Helvetica Neue':fontsize=36:fontcolor=white:x=55:y=978:enable='between(t,{item['start']},{item['end']-.01})'")
filters.append("drawtext=text='SPECTRAL OCEAN / PROTOTYPE 01':font='Helvetica Neue':fontsize=20:fontcolor=0xaacdd8:x=55:y=1030")
video=out/'Spectral_Ocean_Prototype.mp4'
subprocess.run(['ffmpeg','-hide_banner','-v','error','-n','-i',str(clean),'-vf',','.join(filters),'-c:v','libx264','-preset','slow','-crf','17','-pix_fmt','yuv420p','-movflags','+faststart',str(video)],check=True)
data=json.loads(subprocess.run(['ffprobe','-v','error','-show_streams','-show_format','-of','json',str(video)],capture_output=True,text=True,check=True).stdout)
stream=data['streams'][0]
assert stream['width']==1920 and stream['height']==1080 and stream['r_frame_rate']=='30/1'
assert int(stream['nb_frames'])==start*30
subprocess.run(['ffmpeg','-hide_banner','-v','error','-i',str(video),'-f','null','-'],check=True)
(out/'validation.json').write_text(json.dumps(dict(seconds=start,frames=stream['nb_frames'],shots=timeline,video=str(video)),indent=2))
print(str(video))

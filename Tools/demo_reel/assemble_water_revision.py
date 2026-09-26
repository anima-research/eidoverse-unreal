"""Reuse unchanged cuts, replacing 07-09 with new full-resolution renders.

Creates a new revision; never overwrites the first reel or its source frames.
"""
from pathlib import Path
import json,subprocess,sys
here=Path(__file__).resolve().parent
root=Path('/Users/olena/unrealtest/demo_reel')
frames=root/'revision_02_frames';output=root/'output_02'
frames.mkdir(exist_ok=True);(output/'clips').mkdir(parents=True,exist_ok=True)
shots=json.loads((here/'shots.json').read_text())['shots']
manifest=[]
for shot in shots:
    changed=shot['id'] in ('07_surface','08_boarding','09_platform')
    source=root/('water_revision_02' if changed else 'final')/shot['id']
    assert source.is_dir(),source
    link=frames/shot['id']
    if not link.exists():link.symlink_to(source,target_is_directory=True)
    assert link.resolve()==source.resolve()
    # Existing high-quality clips can be reused losslessly for unchanged shots.
    if not changed:
        clip=output/'clips'/(shot['id']+'.mp4')
        original=root/'output'/'clips'/(shot['id']+'.mp4')
        if not clip.exists():clip.symlink_to(original)
    manifest.append({'shot':shot['id'],'frames':str(source),'rerendered':changed})
(output/'revision_sources.json').write_text(json.dumps(manifest,indent=2))
for script in ('assemble_reel.py','validate_reel.py'):
    subprocess.run([sys.executable,str(here/script),'--frames',str(frames),'--output',str(output),'--edition','2'],check=True)

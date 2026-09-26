import unreal,json,os
recipe=json.load(open(os.path.join(os.path.dirname(__file__),'shots.json')))
root='/Game/Underwater/Cinematics/DemoReel';name='LS_DemoReel_Master'
master=unreal.load_asset(root+'/'+name)
if not master:master=unreal.AssetToolsHelpers.get_asset_tools().create_asset(name,root,unreal.LevelSequence,unreal.LevelSequenceFactoryNew())
else:
 for track in master.get_tracks():master.remove_track(track)
track=master.add_track(unreal.MovieSceneCinematicShotTrack)
fps=recipe['fps'];master.set_display_rate(unreal.FrameRate(fps,1));master.set_playback_start(0)
start=0
for shot in recipe['shots']:
 section=track.add_section();section.set_sequence(unreal.load_asset(root+'/LS_'+shot['id']))
 end=start+round(shot['seconds']*fps);section.set_range(start,end);section.set_shot_display_name(shot['title']);start=end
master.set_playback_end(start);unreal.EditorAssetLibrary.save_loaded_asset(master)
unreal.log('REEL_MASTER_READY seconds='+str(start/fps))

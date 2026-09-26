"""Author editable shot sequences. No persistent changes to the gameplay map.
Run through Unreal Python with Movie Render Queue enabled.
"""
import unreal,json,os,math,re
from editor_toolset.toolsets.actor import ActorTools as A
HERE=os.path.dirname(__file__)
RECIPE=json.load(open(os.environ.get('REEL_RECIPE',os.path.join(HERE,'shots.json'))))
ROOT=os.environ.get('REEL_ROOT','/Game/Underwater/Cinematics/DemoReel')
EAS=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
LEVEL=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
assert not unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
if world.get_path_name().split('.')[0]!=RECIPE['map']:assert LEVEL.load_level(RECIPE['map'])
FPS=RECIPE['fps'];AT=unreal.AssetToolsHelpers.get_asset_tools()
for leftover in EAS.get_all_level_actors():
 if leftover.get_actor_label().startswith('Reel temporary camera '):EAS.destroy_actor(leftover)
def prop(o,n,v):o.set_editor_property(n,v)
def vec(v):return unreal.Vector(*v)
def animate(binding,keys,length):
 section=binding.add_track(unreal.MovieScene3DTransformTrack).add_section()
 section.set_range(-120,length+1)
 channels=section.get_all_channels()
 assert len(channels)==9,[(c.get_name(),str(c.get_class())) for c in channels]
 for index,ch in enumerate(channels):ch.set_default(1.0 if index>=6 else 0.0)
 previous_yaw=None
 for sec,p,r in keys:
  yaw=r.yaw
  if previous_yaw is not None:
   while yaw-previous_yaw>180:yaw-=360
   while yaw-previous_yaw<-180:yaw+=360
  previous_yaw=yaw
  values=[p.x,p.y,p.z,r.roll,r.pitch,yaw,1,1,1]
  for ch,value in zip(channels,values):
   ch.add_key(unreal.FrameNumber(round(sec*FPS)),value,interpolation=unreal.MovieSceneKeyInterpolation.LINEAR)
 return section
for shot in RECIPE['shots']:
 name='LS_'+shot['id'];path=ROOT+'/'+name
 seq=unreal.load_asset(path)
 if seq:
  for tr in seq.get_tracks():seq.remove_track(tr)
  for binding in seq.get_bindings():binding.remove()
 else:seq=AT.create_asset(name,ROOT,unreal.LevelSequence,unreal.LevelSequenceFactoryNew())
 seq.set_display_rate(unreal.FrameRate(FPS,1));seq.set_playback_start(0);length=round(shot['seconds']*FPS);seq.set_playback_end(length)
 offset=unreal.Vector()
 if shot.get('anchor'):
  a=next(a for a in EAS.get_all_level_actors() if a.get_actor_label()=='Biome detail | '+shot['anchor'])
  comp=a.get_component_by_class(unreal.HierarchicalInstancedStaticMeshComponent)
  offset=comp.get_instance_transform(shot['anchor_index'],world_space=True).translation
  unreal.log('REEL_ANCHOR '+str(offset))
 camera=EAS.spawn_actor_from_class(unreal.CineCameraActor,unreal.Vector())
 camera.set_actor_label('Reel temporary camera '+shot['id'])
 cc=camera.get_cine_camera_component()
 film=cc.get_editor_property('filmback');prop(film,'sensor_width',36.0);prop(film,'sensor_height',20.25);prop(cc,'filmback',film)
 prop(cc,'current_focal_length',shot['focal']);prop(cc,'current_aperture',8.0)
 focus=cc.get_editor_property('focus_settings');prop(focus,'focus_method',unreal.CameraFocusMethod.DISABLE);prop(cc,'focus_settings',focus)
 prop(cc,'post_process_blend_weight',0.0)
 if shot.get('torch'):
  light=A.add_component(camera,unreal.SpotLightComponent.static_class(),'CaptureTorch')
  prop(light,'mobility',unreal.ComponentMobility.MOVABLE)
  prop(light,'intensity_units',unreal.LightUnits.CANDELAS);light.set_intensity(shot['torch'])
  light.set_attenuation_radius(3000);light.set_inner_cone_angle(28);light.set_outer_cone_angle(48)
  light.set_relative_location(unreal.Vector(20,25,-15),False,True)
  prop(light,'cast_shadows',True)
 binding=seq.add_spawnable_from_instance(camera);binding.set_display_name(shot['title'])
 EAS.destroy_actor(camera)
 keys=[]
 for sec,p,target in shot['camera']:
  position=vec(p)+offset;look=vec(target)+offset
  keys.append((sec,position,unreal.MathLibrary.find_look_at_rotation(position,look)))
 animate(binding,keys,length)
 cut=seq.add_track(unreal.MovieSceneCameraCutTrack).add_section();cut.set_range(-120,length)
 cut.set_camera_binding_id(seq.get_binding_id(binding))
 # Calibration rigs are hidden by sequence-only tracks, never removed from the map.
 for a in EAS.get_all_level_actors():
  if re.match(r'^(Pylon|Platform|Sphere|Label)_\d{3}m',a.get_actor_label()) or a.get_actor_label().startswith('HabSphere_'):
   b=seq.add_possessable(a);track=b.add_track(unreal.MovieSceneVisibilityTrack)
   track.set_property_name_and_path('bHidden','bHidden')
   section=track.add_section();section.set_range(-120,length+1)
   section.get_all_channels()[0].set_default(False)
 if shot.get('sub_surface'):
  sub=next(a for a in EAS.get_all_level_actors() if a.actor_has_tag('OceanSubmarine'))
  sb=seq.add_possessable(sub)
  animate(sb,[(0,unreal.Vector(6000,-2600,-1200),unreal.Rotator(pitch=0,yaw=90,roll=0)),(5.8,unreal.Vector(6000,-2600,35),unreal.Rotator(pitch=0,yaw=90,roll=0)),(8,unreal.Vector(6000,-2600,35),unreal.Rotator(pitch=0,yaw=90,roll=0))],length)
 unreal.EditorAssetLibrary.save_loaded_asset(seq)
 unreal.log('REEL_SEQUENCE_READY '+path)
unreal.log('REEL_BUILD_SUCCESS')

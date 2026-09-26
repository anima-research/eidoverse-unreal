"""Temporary isolated rendering A/B; all console values restored afterward."""
import unreal,runpy
from pathlib import Path
script=Path(__file__).with_name('profile_live.py').read_text()
script=script.replace("setup()\ndef tick", """report['viewport_pixels']=list(pc.get_viewport_size())
report['screen_percentage']=unreal.SystemLibrary.get_console_variable_float_value('r.ScreenPercentage')
report['front_layer_reflections']=lighting_saved_front
report['shadow_pages']=unreal.SystemLibrary.get_console_variable_int_value('r.Shadow.Virtual.MaxPhysicalPages')
setup()
def tick""")
script=script.replace("('full',True,True),('no_capture',False,True),('no_optics',True,False),('neither',False,False)","('full',True,True),('no_tlv',True,True),('no_frontlayer',True,True),('both',True,True)")
script=script.replace("name,cap,fog=phases[phase]","""name,cap,fog=phases[phase]
 unreal.SystemLibrary.execute_console_command(w,'r.TranslucencyLightingVolume '+str(0 if name in ('no_tlv','both') else lighting_saved_tlv))
 unreal.SystemLibrary.execute_console_command(w,'r.Lumen.TranslucencyReflections.FrontLayer.Enable '+str(0 if name in ('no_frontlayer','both') else lighting_saved_front))""")
script=script.replace("report={'tag':globals().get('profile_run_tag',os.environ.get('SPECTRAL_PROFILE_TAG','before'))", "report={'tag':'lighting_fullres_ab'")
script=script.replace("pawn.set_actor_location(previous_position,False,True);pc.set_control_rotation(previous_rotation)","""pawn.set_actor_location(previous_position,False,True);pc.set_control_rotation(previous_rotation)
 unreal.SystemLibrary.execute_console_command(w,'r.TranslucencyLightingVolume '+str(lighting_saved_tlv))
 unreal.SystemLibrary.execute_console_command(w,'r.Lumen.TranslucencyReflections.FrontLayer.Enable '+str(lighting_saved_front))""")
saved_screen_percentage=unreal.SystemLibrary.get_console_variable_float_value('r.ScreenPercentage')
world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
unreal.SystemLibrary.execute_console_command(world,'r.ScreenPercentage 100')
script=script.replace("Path('/Users/olena/unrealtest/demo_reel/spectral_profile_'", "unreal.SystemLibrary.execute_console_command(w,'r.ScreenPercentage '+str(lighting_saved_screen))\n Path('/Users/olena/unrealtest/demo_reel/spectral_profile_'")
ns={'lighting_saved_tlv':1,'lighting_saved_front':unreal.SystemLibrary.get_console_variable_int_value('r.Lumen.TranslucencyReflections.FrontLayer.Enable'),'lighting_saved_screen':saved_screen_percentage}
exec(compile(script,str(Path(__file__).with_name('profile_live.py')),'exec'),ns)

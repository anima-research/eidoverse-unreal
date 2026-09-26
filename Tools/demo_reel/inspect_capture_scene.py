import unreal
for a in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors():
 if a.get_class().get_name() in ('StaticMeshActor','TextRenderActor') and not a.actor_has_tag('MarineEcology'):
  unreal.log('REEL_PROP '+a.get_actor_label()+' '+str(a.get_actor_location()))

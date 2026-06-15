UPDATE creature_template SET AIName = NULL, ScriptName = CASE entry WHEN 29062 THEN 'npc_anub_ar_champion' WHEN 29063 THEN 'npc_anub_ar_necromancer' WHEN 29064 THEN 'npc_anub_ar_cryptfiend' END WHERE entry IN (29062, 29063, 29064);

DELETE FROM smart_scripts WHERE entryorguid = 29062;
DELETE FROM smart_scripts WHERE entryorguid = 29063;
DELETE FROM smart_scripts WHERE entryorguid = 29064;

INSERT INTO spell_script_names (spell_id, ScriptName) VALUES (57731, 'spell_hadronox_web_grab');

DELETE FROM spell_script_names WHERE spell_id = 53035;
DELETE FROM spell_script_names WHERE spell_id = 53036;
DELETE FROM spell_script_names WHERE spell_id = 53037;

UPDATE creature_template SET AIName = '', ScriptName = CASE entry WHEN 29062 THEN 'npc_anub_ar_champion' WHEN 29063 THEN 'npc_anub_ar_necromancer' WHEN 29064 THEN 'npc_anub_ar_cryptfiend' END WHERE entry IN (29062, 29063, 29064);
UPDATE creature_template SET AIName = '', ScriptName = CASE entry WHEN 29117 THEN 'npc_anub_ar_champion' WHEN 29119 THEN 'npc_anub_ar_necromancer' WHEN 29118 THEN 'npc_anub_ar_cryptfiend' END WHERE entry IN (29117, 29119, 29118);

DELETE FROM smart_scripts WHERE entryorguid = 29062;
DELETE FROM smart_scripts WHERE entryorguid = 29063;
DELETE FROM smart_scripts WHERE entryorguid = 29064;

DELETE FROM spell_script_names WHERE spell_id = 53035;
DELETE FROM spell_script_names WHERE spell_id = 53036;
DELETE FROM spell_script_names WHERE spell_id = 53037;

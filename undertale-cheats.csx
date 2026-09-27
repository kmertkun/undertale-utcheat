// Undertale hile yamasi (UndertaleModTool / UTMT CLI icin)
// Kullanim: UndertaleModCli.exe load data.win -s undertale-cheats.csx -o data.win -f
//
// F5 = God mode (HP hep dolu + oyun bitti ekrani engellenir)
// F6 = +100 altin
// F7 = Yavas cekim (oyun yarim hizda akar, mermilerden kacmak kolaylasir)
// F8 = Bilgi paneli (SOUL / karakter X-Y koordinati, HP, altin)

EnsureDataLoaded();

if (Data.Code.ByName("gml_Object_obj_time_Step_0") != null)
{
    ScriptError("Bu data.win zaten yamali gorunuyor (obj_time Step_0 mevcut).");
    return;
}

UndertaleModLib.Compiler.CodeImportGroup importGroup = new(Data)
{
    MainThreadAction = MainThreadAction
};

// obj_time her odada bulunan kalici (persistent) obje. Normal Step eventi yok, yenisini ekliyoruz.
importGroup.QueueReplace("gml_Object_obj_time_Step_0", @"
if (!variable_global_exists(""cheat_god""))
{
    global.cheat_god = 0;
    global.cheat_slow = 0;
    global.cheat_rs = 0;
    global.cheat_hud = 1;
    global.cheat_msg = """";
    global.cheat_msgt = 0;
}
if (keyboard_check_pressed(vk_f5))
{
    global.cheat_god = !global.cheat_god;
    if (global.cheat_god)
        global.cheat_msg = ""GOD MODE: ACIK"";
    else
        global.cheat_msg = ""GOD MODE: KAPALI"";
    global.cheat_msgt = 60;
}
if (keyboard_check_pressed(vk_f6))
{
    global.gold += 100;
    global.cheat_msg = ""+100 ALTIN"";
    global.cheat_msgt = 60;
}
if (keyboard_check_pressed(vk_f7))
{
    global.cheat_slow = !global.cheat_slow;
    if (global.cheat_slow)
        global.cheat_msg = ""YAVAS CEKIM: ACIK"";
    else
        global.cheat_msg = ""YAVAS CEKIM: KAPALI"";
    global.cheat_msgt = 60;
}
if (keyboard_check_pressed(vk_f8))
    global.cheat_hud = !global.cheat_hud;
if (global.cheat_god)
{
    if (global.hp < global.maxhp)
        global.hp = global.maxhp;
}
if (global.cheat_slow)
{
    if (room_speed != 15)
    {
        global.cheat_rs = room_speed;
        room_speed = 15;
    }
}
else if (global.cheat_rs > 0)
{
    room_speed = global.cheat_rs;
    global.cheat_rs = 0;
}
if (global.cheat_msgt > 0)
    global.cheat_msgt -= 1;
");

importGroup.QueueAppend("gml_Object_obj_time_Draw_64", @"
if (variable_global_exists(""cheat_god""))
{
    draw_set_font(fnt_maintext);
    if (global.cheat_hud)
    {
        var ty = 4;
        draw_set_color(c_yellow);
        var gs = ""-"";
        if (global.cheat_god)
            gs = ""ON"";
        var ss = ""-"";
        if (global.cheat_slow)
            ss = ""ON"";
        draw_text(6, ty, ""F5 GOD:"" + gs + ""  F6 +100G  F7 SLOW:"" + ss + ""  F8 HUD"");
        ty += 18;
        draw_set_color(c_aqua);
        if (instance_exists(obj_heart))
        {
            draw_text(6, ty, ""SOUL X:"" + string_format(obj_heart.x, 0, 1) + ""  Y:"" + string_format(obj_heart.y, 0, 1));
            ty += 18;
        }
        else if (instance_exists(obj_mainchara))
        {
            draw_text(6, ty, ""FRISK X:"" + string_format(obj_mainchara.x, 0, 1) + ""  Y:"" + string_format(obj_mainchara.y, 0, 1));
            ty += 18;
        }
        draw_text(6, ty, ""HP:"" + string(global.hp) + ""/"" + string(global.maxhp) + ""  G:"" + string(global.gold));
    }
    if (global.cheat_msgt > 0)
    {
        draw_set_color(c_lime);
        draw_text(6, 80, global.cheat_msg);
    }
    draw_set_color(c_white);
}
");

// Oyun bitti ekranina giden tum yollar (savas, Mettaton date, fake heart) bu script'ten gecer.
importGroup.QueuePrepend("gml_Script_scr_gameoverb", @"
if (variable_global_exists(""cheat_god""))
{
    if (global.cheat_god)
    {
        global.hp = global.maxhp;
        exit;
    }
}
");

importGroup.Import();
ScriptMessage("Undertale hile yamasi uygulandi: F5 God, F6 +100G, F7 Yavas cekim, F8 HUD");

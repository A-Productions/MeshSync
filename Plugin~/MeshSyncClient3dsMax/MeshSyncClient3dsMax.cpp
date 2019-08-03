#include "pch.h"
#include "msmaxContext.h"

HINSTANCE g_msmax_hinstance;

BOOL WINAPI DllMain(HINSTANCE hinstDLL, ULONG fdwReason, LPVOID lpvReserved)
{
    switch (fdwReason) {
    case DLL_PROCESS_ATTACH:
        g_msmax_hinstance = hinstDLL;
        MaxSDK::Util::UseLanguagePackLocale();
        msmaxGetContext(); // initialize instance
        DisableThreadLibraryCalls(hinstDLL);
        break;
    }
    return(TRUE);
}

msmaxAPI const TCHAR* LibDescription()
{
    return _T("UnityMeshSync for 3ds Max (Release " msPluginVersionStr ") (Unity Technologies)");
}

msmaxAPI int LibNumberClasses()
{
    return 0;
}

msmaxAPI ClassDesc* LibClassDesc(int i)
{
    return nullptr;
}

msmaxAPI ULONG LibVersion()
{
    return VERSION_3DSMAX;
}

msmaxAPI ULONG CanAutoDefer()
{
    return 0;
}


#include <maxscript\macros\define_instantiation_functions.h>
def_struct_primitive(Window, UnityMeshSync, "Window");
def_struct_primitive(ServerStatus, UnityMeshSync, "ServerStatus");
def_struct_primitive(Settings, UnityMeshSync, "Settings");
def_struct_primitive(Send, UnityMeshSync, "Send");
def_struct_primitive(Import, UnityMeshSync, "Import");
def_struct_primitive(ExportCache, UnityMeshSync, "ExportCache");

Value* Window_cf(Value** arg_list, int count)
{
    if (count >= 1 && wcscmp(arg_list[0]->to_string(), L"close") == 0) {
        msmaxGetContext().closeSettingWindow();
    }
    else {
        msmaxGetContext().openSettingWindow();
    }
    return &ok;
}

Value* ServerStatus_cf(Value** arg_list, int count)
{
    if (count == 0) {
        return bool_result(msmaxGetContext().isServerAvailable());
    }
    else {
        std::wstring k = arg_list[0]->to_string();
        if (k == L"isAvailable")
            return bool_result(msmaxGetContext().isServerAvailable());
        else if (k == L"errorMessage")
            return new String(ms::ToWCS(msmaxGetContext().getErrorMessage()).c_str());
    }
    return &undefined;
}

Value* Settings_cf(Value** arg_list, int count)
{
    struct GetterSetter
    {
        using getter_t = std::function<Value*()>;
        using setter_t = std::function<void(Value*)>;
        getter_t getter;
        setter_t setter;
    };

    static std::map<std::wstring, GetterSetter> s_table;
    if (s_table.empty()) {
#define Entry(Name, From, To) s_table[L#Name] = {\
    []() -> Value* { return From(msmaxGetSettings().Name); },\
    [](Value *v) -> void{ msmaxGetSettings().Name = v->To(); } }

        Entry(timeout_ms,           Integer::intern, to_int);
        Entry(scale_factor,         Float::intern, to_float);
        Entry(auto_sync,            bool_result, to_bool);

        Entry(sync_meshes,          bool_result, to_bool);
        Entry(sync_normals,         bool_result, to_bool);
        Entry(sync_uvs,             bool_result, to_bool);
        Entry(sync_colors,          bool_result, to_bool);
        Entry(flip_faces,           bool_result, to_bool);
        Entry(make_double_sided,    bool_result, to_bool);
        Entry(ignore_non_renderable,bool_result, to_bool);
        Entry(bake_modifiers,       bool_result, to_bool);
        Entry(use_render_meshes,    bool_result, to_bool);
        Entry(flatten_hierarchy,    bool_result, to_bool);

        Entry(sync_bones,           bool_result, to_bool);
        Entry(sync_blendshapes,     bool_result, to_bool);
        Entry(sync_cameras,         bool_result, to_bool);
        Entry(sync_lights,          bool_result, to_bool);
        Entry(sync_textures,        bool_result, to_bool);

        Entry(anim_time_scale,          Float::intern, to_float);
        Entry(anim_sample_rate,         Float::intern, to_float);
        Entry(anim_keyframe_reduction,  bool_result, to_bool);
        Entry(anim_keep_flat_curves,    bool_result, to_bool);

        Entry(multithreaded,            bool_result, to_bool);

#undef Entry
    }

    auto& settings = msmaxGetSettings();
    if (count == 1) {
        one_value_local(result);
        auto it = s_table.find(arg_list[0]->to_string());
        if (it != s_table.end()) {
            vl.result = it->second.getter();
            return_value(vl.result);
        }
        else {
            return &undefined;
        }
    }
    else {
        for (int i = 0; i + 1 < count; /**/) {
            auto it = s_table.find(arg_list[i++]->to_string());
            if (it != s_table.end()) {
                it->second.setter(arg_list[i++]);
            }
        }
    }
    return &ok;
}

Value* Send_cf(Value** arg_list, int count)
{
    auto target = msmaxExportTarget::Objects;
    auto scope = msmaxObjectScope::All;

    // parse args
    for (int i = 0; i < count; /**/) {
        std::wstring name = arg_list[i++]->to_string();
        if (i + 1 <= count) {
            if (name == L"target") {
                std::wstring value = arg_list[i++]->to_string();
                if (value == L"objects")
                    target = msmaxExportTarget::Objects;
                else if (value == L"materials")
                    target = msmaxExportTarget::Materials;
                else if (value == L"animations")
                    target = msmaxExportTarget::Animations;
                else if (value == L"everything")
                    target = msmaxExportTarget::Everything;
            }
            else if (name == L"scope") {
                std::wstring value = arg_list[i++]->to_string();
                if (value == L"all")
                    scope = msmaxObjectScope::All;
                else if (value == L"selected")
                    scope = msmaxObjectScope::Selected;
                else if (value == L"updated")
                    scope = msmaxObjectScope::Updated;
            }
        }
    }

    // do send
    msmaxSendScene(target, scope);
    return &ok;
}

Value* Import_cf(Value** arg_list, int count)
{
    msmaxGetContext().recvScene();
    return &ok;
}

// e.g. UnityMeshSync.ExportCache path:"C:\tmp\hoge.sc"
Value* ExportCache_cf(Value** arg_list, int count)
{
    msmaxCacheExportSettings settings;

    // parse args
    for (int i = 0; i < count; /**/) {
        std::wstring name = arg_list[i++]->to_string();
        if (i + 1 <= count) {
            if (name == L"path") {
                settings.path = mu::ToMBS(arg_list[i++]->to_string());
            }
            else if (name == L"object_scope") {
                std::wstring value = arg_list[i++]->to_string();
                if (value == L"all")
                    settings.object_scope = msmaxObjectScope::All;
                else if (value == L"selected")
                    settings.object_scope = msmaxObjectScope::Selected;
            }
            else if (name == L"frame_range") {
                std::wstring value = arg_list[i++]->to_string();
                if (value == L"current")
                    settings.frame_range = msmaxFrameRange::CurrentFrame;
                else if (value == L"all")
                    settings.frame_range = msmaxFrameRange::AllFrames;
            }
            else if (name == L"material_frame_range") {
                std::wstring value = arg_list[i++]->to_string();
                if (value == L"none")
                    settings.material_frame_range = msmaxMaterialFrameRange::None;
                else if (value == L"one")
                    settings.material_frame_range = msmaxMaterialFrameRange::OneFrame;
                else if (value == L"all")
                    settings.material_frame_range = msmaxMaterialFrameRange::AllFrames;
            }
            else if (name == L"zstd_compression_level") settings.zstd_compression_level = arg_list[i++]->to_int();
            else if (name == L"sample_rate")            settings.sample_rate = arg_list[i++]->to_float();
            else if (name == L"bake_modifiers")         settings.bake_modifiers = arg_list[i++]->to_bool();
            else if (name == L"use_render_meshes")      settings.use_render_meshes = arg_list[i++]->to_bool();
            else if (name == L"flatten_hierarchy")      settings.flatten_hierarchy = arg_list[i++]->to_bool();
            else if (name == L"merge_meshes")           settings.merge_meshes = arg_list[i++]->to_bool();
            else if (name == L"strip_normals")          settings.strip_normals = arg_list[i++]->to_bool();
            else if (name == L"strip_tangents")         settings.strip_tangents = arg_list[i++]->to_bool();
        }
    }
    msmaxGetContext().exportCache(settings);
    return &ok;
}

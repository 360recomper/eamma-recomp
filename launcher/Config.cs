using System.Text.Json;

namespace EAMMALauncher;

/// <summary>All paths are relative to the folder holding the launcher exe (the play folder with eamma.exe).</summary>
public static class Paths
{
    public static readonly string Root =
        Path.GetDirectoryName(Environment.ProcessPath) ?? AppContext.BaseDirectory;
    /// <summary>add_executable(eamma ...) in CMakeLists.txt.</summary>
    public static string GameExe => Path.Combine(Root, "eamma.exe");
    /// <summary>A plain x86-64 build, if a release ever ships two exes (Cpu.GameExeForThisCpu picks one). None so far.</summary>
    public static string CompatExe => Path.Combine(Root, "eamma_compat.exe");
    /// <summary>The game DLL: the native renderer is compiled into it, not into the exe (cmake/native_renderer.cmake).</summary>
    public static string GameDll => Path.Combine(Root, "eamma_MMADLLzf_xex.dll");
    public static string Config => Path.Combine(Root, "launcher.json");

    /// <summary>The SDK loads "&lt;app name&gt;.toml" beside the exe (rex_app.cpp); the app name is "eamma" (src/eamma_app.h).</summary>
    public static string Toml => Path.Combine(Root, "eamma.toml");
    public static bool IsInstalled => File.Exists(GameExe);

    static string TomlPath(string key, string fallback)
    {
        try { var m = new EAMMALauncher.Toml().Get(key); if (!string.IsNullOrWhiteSpace(m)) return Path.GetFullPath(m, Root); }
        catch { }
        return fallback;
    }

    /// <summary>The game files (the extracted disc): game_data_root from eamma.toml, else assets\ next to the exe
    /// (EammaApp::OnConfigurePaths uses the same default).</summary>
    public static string Assets => TomlPath("game_data_root", Path.Combine(Root, "assets"));
    /// <summary>Saves and DLC: user_data_root from eamma.toml, else userdata\ next to the exe. NOTE: the game's own default
    /// without the cvar is the per-user app folder (SDK), so only trust this for writing when the toml sets it.</summary>
    public static string UserData => TomlPath("user_data_root", Path.Combine(Root, "userdata"));
    /// <summary>Setup is done once the disc's default.xex is in the game files.</summary>
    public static bool AssetsReady => File.Exists(Path.Combine(Assets, "default.xex"));

    /// <summary>The game's mods folder: the SDK's mods_root from eamma.toml when set, else mods\ next to the exe. Not used by
    /// a tab yet (mod support is WP11).</summary>
    public static string Mods => TomlPath("mods_root", Path.Combine(Root, "mods"));
}

/// <summary>launcher.json next to the exe: launcher-only state (the game's own settings live in eamma.toml).</summary>
public class Config
{
    /// <summary>Ask for a newer release when the launcher starts. Only read when Updater.Enabled.</summary>
    public bool CheckForUpdates { get; set; } = true;

    static readonly JsonSerializerOptions Opts = new() { WriteIndented = true };

    public static Config Load()
    {
        try { return JsonSerializer.Deserialize<Config>(File.ReadAllText(Paths.Config)) ?? new(); }
        catch { return new(); }
    }

    public void Save() => File.WriteAllText(Paths.Config, JsonSerializer.Serialize(this, Opts));
}

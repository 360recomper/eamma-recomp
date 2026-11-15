namespace EAMMALauncher;

/// <summary>Every name the launcher shows or looks for, in one place. The look is plain text and colours drawn at run
/// time (Tabs/Shell.cs): there is no artwork in this launcher yet.</summary>
public static class Branding
{
    /// <summary>The two lines of the sidebar mark and the Play banner.</summary>
    public const string Title1 = "EA MMA", Title2 = "RECOMP";
    public const string ProductName = "EA MMA Recomp";
    /// <summary>Caption of the launcher's own message boxes.</summary>
    public const string LauncherTitle = "EA MMA Launcher";
    /// <summary>AssemblyName in EAMMALauncher.csproj + ".exe".</summary>
    public const string LauncherExe = "EA MMA Launcher.exe";
    /// <summary>What the player has to own, as the Setup page names it.</summary>
    public const string GameName = "EA Sports MMA";
    /// <summary>The game exe is told the launcher started it (it then leaves the error dialog to the launcher):
    /// src/eamma_renderer_fail.cpp.</summary>
    public const string LauncherEnv = "EAMMA_LAUNCHER";
}

using System.Diagnostics;

namespace EAMMALauncher.Tabs;

public class PlayTab : ILauncherTab
{
    readonly Panel root = new() { Dock = DockStyle.Fill, BackColor = Theme.Bg };
    readonly Button play;
    readonly Label info = Ui.Text("");
    readonly Label header = Ui.Text("");
    readonly FlowLayoutPanel updRow = new() { AutoSize = true, WrapContents = false, Margin = new Padding(0, Theme.S(8), 0, 0), Visible = false };
    readonly Label updText = Ui.Text("", true, 330);
    readonly Button updBtn;
    readonly ProgressBar updBar = new AccentBar { Width = Theme.S(540), Height = Theme.S(14), Maximum = 1000, Visible = false };
    readonly Label updInfo = Ui.Text("");
    readonly CheckBox checkUpd = new DarkCheck { Text = "Check for updates when the launcher starts", AutoSize = true };
    Updater.Release? latest;
    bool loading, updating, buildingCache;

    /// <summary>Raised (UI thread) when the check finds a newer release.</summary>
    public event Action<Updater.Release>? UpdateFound;
    /// <summary>Raised after an update that did not need a restart.</summary>
    public event Action? Updated;

    public string Title => "Play";
    public Control View => root;

    static string HeaderText => Branding.ProductName + "   v" + AppVersion.Installed + "  (launcher " + AppVersion.Launcher + ")";

    public PlayTab()
    {
        var col = Ui.Column();
        header.Text = HeaderText;
        col.Controls.Add(header);
        play = new FlatButton { Kind = BtnKind.Play, Text = "Play", Width = Theme.S(320), Height = Theme.S(82), Margin = new Padding(-Theme.S(7), Theme.S(6), 0, Theme.S(8)) };
        play.Click += (_, _) => Launch();
        col.Controls.Add(play);
        col.Controls.Add(info);
        checkUpd.CheckedChanged += (_, _) => { if (!loading) { var c = Config.Load(); c.CheckForUpdates = checkUpd.Checked; c.Save(); } };
        updBtn = Ui.Primary(Ui.AutoBtn("Update", async (_, _) => await RunUpdate()));
        updText.ForeColor = Theme.AccentText; updText.Margin = new Padding(0, Theme.S(13), Theme.S(10), 0);
        updRow.Controls.AddRange(new Control[] { updText, updBtn, Ui.AutoBtn("What's new", (_, _) => OpenUrl(latest?.Page ?? Updater.ReleasesPage)) });
#pragma warning disable CS0162 // the update controls exist only when the updater is on (Updater.Enabled)
        if (Updater.Enabled)
        {
            col.Controls.Add(updRow);
            col.Controls.Add(updBar);
            col.Controls.Add(updInfo);
            col.Controls.Add(Ui.Head("Updates"));
            col.Controls.Add(checkUpd);
            col.Controls.Add(Ui.AutoBtn("Check for updates now", async (_, _) => await CheckForUpdates(true)));
        }
#pragma warning restore CS0162
        col.Controls.Add(Ui.Text("Play uses your settings in eamma.toml (Settings tab)."));
        root.Controls.Add(col);
        root.Controls.Add(new PlayBanner());
    }

    /// <summary>--screenshot-all: show the update row and bar as during a download.</summary>
    internal void PreviewUpdate()
    {
        updText.Text = $"Update available: v9.9 (you have v{AppVersion.Installed})";
        updRow.Visible = true;
        updBar.Visible = true; updBar.Value = 420;
        updInfo.Text = "Downloading EAMMA-Recomp-v9.9.zip...";
    }

    bool CanPlay => Paths.IsInstalled && Paths.AssetsReady && !updating && !buildingCache;

    public void OnShown()
    {
        loading = true;
        var cfg = Config.Load();
        checkUpd.Checked = cfg.CheckForUpdates;
        loading = false;
        play.Enabled = CanPlay;
        if (!buildingCache)
            info.Text = !Paths.IsInstalled ? "eamma.exe is not next to this launcher. Put the launcher in the game folder."
                : !Paths.AssetsReady ? $"The game files are not set up yet. Open the Setup tab and choose your {Branding.GameName} .iso."
                : "";
    }

    /// <summary>Asks for the latest release; shows the update row when it is newer. Quiet on errors unless asked. Does
    /// nothing (no network call) while the updater is off.</summary>
    public async Task CheckForUpdates(bool manual)
    {
#pragma warning disable CS0162
        if (!Updater.Enabled) return;
#pragma warning restore CS0162
        if (updating) return;
        if (manual) updInfo.Text = "Checking...";
        try
        {
            latest = await Updater.FetchLatest();
            if (latest.IsNewer)
            {
                updText.Text = $"Update available: v{latest.Version} (you have v{AppVersion.Installed})";
                updRow.Visible = true;
                updInfo.Text = latest.Game == null ? "This release has no game zip; use \"What's new\"." : "";
                updBtn.Enabled = latest.Game != null;
                UpdateFound?.Invoke(latest);
            }
            else if (manual) updInfo.Text = $"Up to date (v{AppVersion.Installed}; latest release v{latest.Version}).";
        }
        catch (Exception ex) { if (manual) updInfo.Text = "Could not check for updates: " + ex.Message; }
    }

    async Task RunUpdate()
    {
        var rel = latest;
        if (rel?.Game == null) return;
        if (Updater.GameRunning()) { updInfo.Text = "Close the game first."; return; }
        if (DarkBox.Show($"Download v{rel.Version} ({rel.Game.Size / 1048576.0:0} MB) and replace the changed program files?\n\n" +
                "Your settings (eamma.toml), saves, mods and game files are not touched. Replaced files are kept in update-backups.",
                "Update", MessageBoxButtons.OKCancel, MessageBoxIcon.Question) != DialogResult.OK) return;
        updating = true; updBtn.Enabled = play.Enabled = false; updBar.Visible = true; updBar.Value = 0;
        try
        {
            updInfo.Text = $"Downloading {rel.Game.Name}...";
            var zip = await Updater.Download(rel.Game, new Progress<double>(f => updBar.Value = (int)Math.Clamp(f * 1000, 0, 1000)), CancellationToken.None);
            updInfo.Text = "SHA-256 ok. Replacing files...";
            var res = await Task.Run(() => Updater.Apply(zip, rel.Tag));
            Updater.CleanDownloads();
            updRow.Visible = false;
            if (res.RestartExe != null)
            {
                Process.Start(new ProcessStartInfo(res.RestartExe, "--updated") { WorkingDirectory = Paths.Root, UseShellExecute = false });
                Application.Exit();
                return;
            }
            updInfo.Text = res.Log.LastOrDefault() ?? $"Updated to v{rel.Version}.";
            header.Text = HeaderText;
            Updated?.Invoke();
        }
        catch (Exception ex) { updInfo.Text = "Update failed: " + ex.Message; updBtn.Enabled = true; }
        finally { updating = false; updBar.Visible = false; play.Enabled = CanPlay; }
    }

    static void OpenUrl(string url) => Process.Start(new ProcessStartInfo(url) { UseShellExecute = true });

    async void Launch()
    {
        try
        {
            if (Updater.GameRunning()) { info.Text = "The game is already running."; return; }
            if (!NativeRenderer.Available)
            {
                // Native-only: the launcher never starts the emulated renderer. A build without the native renderer
                // compiled in (build.bat nonative / census) is a development build: run eamma.exe by hand.
                info.Text = "This eamma.exe was built without the native renderer, so the launcher can't start it.";
                return;
            }
            // No fallback. A failed build shows what failed and what to do; the game is not started.
            bool force = false;
            while (force || !ShaderCache.Current)
            {
                buildingCache = true; play.Enabled = false;
                info.Text = "Building the shader cache from your game files. This only happens once...";
                ShaderCache.Result? failed;
                try { bool f = force; failed = await Task.Run(() => NativeRenderer.PrepareCache(f)); }
                finally { buildingCache = false; play.Enabled = CanPlay; }
                info.Text = "";
                if (failed == null) break;
                if (RendererError.ShowCacheFailure(failed) != DialogResult.Retry)
                {
                    info.Text = "The game was not started: the shader cache could not be built (details: launcher-native.log).";
                    return;
                }
                force = true;
            }
            NativeRenderer.PlayFrom(root);
        }
        catch (Exception ex)
        {
            info.Text = "Could not start the game: " + ex.Message;
            play.Enabled = CanPlay;
        }
    }
}

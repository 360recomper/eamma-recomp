namespace EAMMALauncher.Tabs;

/// <summary>First-run setup: the user's own disc image -> assets\, then the shader cache. The DLC section (UFC's) is
/// only added when Setup.DlcUi is true.</summary>
public class SetupTab : ILauncherTab
{
    readonly FlowLayoutPanel col = Ui.Column();
    readonly Label state = Ui.Text("", true);
    readonly Label status = Ui.Text("");
    readonly ProgressBar bar = new AccentBar { Width = Theme.S(540), Height = Theme.S(18), Maximum = 1000 };
    readonly Label current = Ui.Text("");
    readonly Button isoBtn, cancelBtn;
    readonly List<Button> dlcBtns = new();
    CancellationTokenSource? cts;

    public string Title => "Setup";
    public Control View => col;

    public SetupTab()
    {
        col.Controls.Add(Ui.Head("Game files"));
        col.Controls.Add(state);
        col.Controls.Add(Ui.Text($"Choose your own {Branding.GameName} disc image (.iso). The launcher checks that it is the right game, " +
            "then copies the game files (about 6.3 GB) into the assets folder next to this launcher and builds the shader cache. " +
            "This takes a few minutes. If you cancel, run it again to continue where it stopped."));
        isoBtn = Ui.Primary(Ui.Btn("Choose game ISO...", async (_, _) => await RunIso(), 170));
        col.Controls.Add(isoBtn);

#pragma warning disable CS0162 // Setup.DlcUi is a constant
        if (Setup.DlcUi)
        {
            col.Controls.Add(Ui.Head("DLC (optional)"));
            col.Controls.Add(Ui.Text("Choose a folder or a zip of your Xbox 360 DLC packages. They are unpacked into the userdata folder, " +
                "and the game then lists them as purchased."));
            var row = new FlowLayoutPanel { AutoSize = true, WrapContents = false };
            dlcBtns.Add(Ui.Btn("Choose DLC folder...", async (_, _) => await RunDlcFolder(), 170));
            dlcBtns.Add(Ui.Btn("Choose DLC zip/package...", async (_, _) => await RunDlcFile(), 190));
            row.Controls.AddRange(dlcBtns.ToArray<Control>());
            col.Controls.Add(row);
        }
#pragma warning restore CS0162

        cancelBtn = Ui.Btn("Cancel", (_, _) => cts?.Cancel(), 90);
        cancelBtn.Enabled = false;
        col.Controls.Add(bar);
        col.Controls.Add(current);
        col.Controls.Add(cancelBtn);
        col.Controls.Add(status);
    }

    void Busy(bool busy)
    {
        isoBtn.Enabled = !busy;
        foreach (var b in dlcBtns) b.Enabled = !busy;
        cancelBtn.Enabled = busy;
    }

    /// <summary>--screenshot-all: the page as during an ISO import.</summary>
    internal void PreviewBusy()
    {
        Busy(true);
        bar.Value = 420; current.Text = @"42%  data/xenon-gen/bigs/shader_big.ast";
        status.Text = "Copying game files...";
    }

    public void OnShown() =>
        state.Text = Paths.AssetsReady ? "Installed: the game files are in " + Paths.Assets
            : "Not set up yet. Choose your ISO to begin; Play stays off until this is done.";

    async Task Run(Func<IProgress<(string, double)>, CancellationToken, string> work)
    {
        cts = new CancellationTokenSource();
        Busy(true);
        status.Text = "";
        var prog = new Progress<(string name, double frac)>(p =>
        {
            bar.Value = (int)Math.Clamp(p.frac * 1000, 0, 1000);
            // frac < 0: a step with no percentage to show.
            current.Text = p.name.Length == 0 ? "" : p.frac < 0 ? p.name : $"{p.frac * 100:0}%  {p.name}";
        });
        try { status.Text = await Task.Run(() => work(prog, cts.Token)); }
        catch (OperationCanceledException) { status.Text = "Cancelled. Choose the same file again to continue where it stopped."; }
        catch (Exception ex) { status.Text = "Error: " + ex.Message; }
        finally
        {
            Busy(false);
            bar.Value = 0; current.Text = "";
            OnShown();
        }
    }

    Task RunIso()
    {
        using var dlg = new OpenFileDialog { Title = Branding.GameName + " disc image", Filter = "Disc image (*.iso)|*.iso|All files|*.*" };
        if (dlg.ShowDialog() != DialogResult.OK) return Task.CompletedTask;
        var iso = dlg.FileName;
        return Run((p, ct) =>
        {
            Setup.ImportIso(iso, Paths.Assets, p, ct);
            return Setup.FinishImport(Paths.Assets, p, ct);
        });
    }

    Task RunDlcFolder()
    {
        using var dlg = new FolderBrowserDialog { Description = "Folder with your Xbox 360 DLC packages (or zips of them)" };
        return dlg.ShowDialog() == DialogResult.OK ? RunDlc(dlg.SelectedPath) : Task.CompletedTask;
    }

    Task RunDlcFile()
    {
        using var dlg = new OpenFileDialog { Title = "DLC zip or package", Filter = "Zip or package|*.zip;*|All files|*.*" };
        return dlg.ShowDialog() == DialogResult.OK ? RunDlc(dlg.FileName) : Task.CompletedTask;
    }

    Task RunDlc(string src) =>
        Run((p, ct) => string.Join("\n", Setup.ImportDlc(src, Paths.UserData, p, ct)));
}

using System.IO.Compression;
using System.Text;

namespace EAMMALauncher;

/// <summary>First-run work: disc image -> assets\, then the shader cache. DLC packages -> userdata (not offered in the
/// Setup page yet, see <see cref="DlcUi"/>).</summary>
public static class Setup
{
    /// <summary>EA Sports MMA as this recomp was made from it: NOTES.md line 3 ("Title 454108FC, media 324DBAD4, version
    /// 0.0.0.2, disc build 2010-09-15"), checked 2026-11-15 against the execution info of assets\default.xex. Which
    /// region that disc is, and whether other regions' discs have another media id, is not known: a different media id
    /// is refused with a message that says so.</summary>
    public const uint TitleId = 0x454108FC, MediaId = 0x324DBAD4;

    /// <summary>TODO(WP10): whether EA Sports MMA has DLC packages worth installing is not established (NOTES.md does
    /// not say). The installer below is UFC's, generic STFS code with this game's title id, reachable only through
    /// --setup-dlc; the Setup page shows its DLC section only when this is true.</summary>
    public const bool DlcUi = false;

    /// <summary>Checks the image is EA Sports MMA, then extracts it into dest (resumable).</summary>
    public static void ImportIso(string iso, string dest, IProgress<(string, double)> progress, CancellationToken ct)
    {
        progress.Report(("Reading the disc image...", 0));
        using var xgd = new Xgd(iso);
        var xex = xgd.Find("default.xex") ?? throw new InvalidDataException("default.xex not found: this is not a game disc.");
        var info = xgd.ReadXexInfo(xex);
        if (info.TitleId != TitleId)
            throw new InvalidDataException($"This is not {Branding.GameName} (title {info.TitleId:X8}, expected {TitleId:X8}).");
        if (info.MediaId != MediaId)
            throw new InvalidDataException($"This is a different release of {Branding.GameName} (media {info.MediaId:X8}). The recomp supports the disc with media {MediaId:X8}.");
        CheckSpace(dest, xgd);
        xgd.ExtractAll(dest, progress, ct);
        if (!File.Exists(Path.Combine(dest, "default.xex"))) throw new InvalidDataException("default.xex missing after copy.");
    }

    /// <summary>The step after the disc files are in place, on builds with the native renderer: builds its shader cache.
    /// A failure doesn't undo the install and comes back as text (Play builds it again and shows the error dialog if it
    /// still fails; there is no renderer to fall back to); only cancelling throws.</summary>
    public static string FinishImport(string gameData, IProgress<(string, double)> progress, CancellationToken ct)
    {
        const string Done = "Game files installed. Open the Play tab to start.";
        if (!NativeRenderer.Available) return Done;
        const string Busy = "Building the shader cache...";
        progress.Report((Busy, -1));
        var r = ShaderCache.Build(gameData, new Progress<string>(_ => progress.Report((Busy, -1))), ct);
        if (r.Ok) return Done;
        return "Game files installed, but the shader cache could not be built:\n" + r.Message +
            (r.ExitCode is int c ? $" (step code {c})" : "") +
            "\nPlay builds it again and says what to do if it still fails. Details: launcher-native.log";
    }

    static void CheckSpace(string dest, Xgd xgd)
    {
        try
        {
            long have = 0;
            if (Directory.Exists(dest))
                foreach (var f in new DirectoryInfo(dest).EnumerateFiles("*", SearchOption.AllDirectories)) have += f.Length;
            long need = xgd.TotalBytes - have;
            if (need <= 0) return;
            long free = FreeBytes(Path.GetFullPath(dest));
            if (free >= 0 && free < need + (256L << 20))
                throw new IOException($"Not enough free space for {dest}: need about {need / 1073741824.0:0.0} GB, " +
                                      $"{free / 1073741824.0:0.0} GB free.");
        }
        catch (IOException) { throw; }
        catch { /* space check is best effort */ }
    }

    /// <summary>Free bytes for the folder itself (its nearest existing parent), -1 if unknown. Asked per folder, not per
    /// drive root: under Wine/Proton (Steam Deck) Z:\ is the small read-only system partition while the game folder sits
    /// on /home.</summary>
    static long FreeBytes(string path)
    {
        string? dir = path;
        while (dir != null && !Directory.Exists(dir)) dir = Path.GetDirectoryName(dir);
        if (dir == null) return -1;
        if (GetDiskFreeSpaceExW(dir.EndsWith('\\') ? dir : dir + "\\", out ulong avail, out _, out _))
            return (long)Math.Min(avail, (ulong)long.MaxValue);
        return -1;
    }

    [System.Runtime.InteropServices.DllImport("kernel32.dll", CharSet = System.Runtime.InteropServices.CharSet.Unicode, SetLastError = true)]
    [return: System.Runtime.InteropServices.MarshalAs(System.Runtime.InteropServices.UnmanagedType.Bool)]
    static extern bool GetDiskFreeSpaceExW(string dir, out ulong freeToCaller, out ulong total, out ulong totalFree);

    /// <summary>Installs Xbox 360 DLC packages from a package, a zip or a folder (recursively) into userdata.
    /// Unverified for this game (see <see cref="DlcUi"/>).</summary>
    public static List<string> ImportDlc(string source, string userdata, IProgress<(string, double)> progress, CancellationToken ct)
    {
        var log = new List<string>();
        int ok = 0;
        void One(string file, string name)
        {
            progress.Report((name, 0));
            try
            {
                using var pkg = new Stfs(file);
                if (pkg.ContentType != 2) { log.Add($"skipped (not DLC): {name}"); return; }
                if (pkg.TitleId != TitleId) { log.Add($"skipped (not {Branding.GameName}): {name}"); return; }
                var titleDir = Path.Combine(userdata, "0000000000000000", TitleId.ToString("X8"));
                var dest = Path.Combine(titleDir, "00000002", name);
                var tmp = dest + ".partial";
                if (Directory.Exists(tmp)) Directory.Delete(tmp, true);
                var (n, bytes) = pkg.Extract(tmp, ct);
                if (Directory.Exists(dest)) Directory.Delete(dest, true);
                Directory.CreateDirectory(Path.GetDirectoryName(dest)!);
                Directory.Move(tmp, dest);
                var hdr = Path.Combine(titleDir, "Headers", "00000002");
                Directory.CreateDirectory(hdr);
                File.WriteAllBytes(Path.Combine(hdr, name + ".header"), HeaderBytes(pkg.Title, name));
                log.Add($"ok   {pkg.Title}  [{name}]  {n} files, {bytes / 1048576.0:0} MB");
                ok++;
            }
            catch (InvalidDataException ex) when (ex.Message == "not an STFS package") { /* unrelated file */ }
            catch (OperationCanceledException) { throw; }
            catch (Exception ex) { log.Add($"failed: {name} ({ex.Message})"); }
        }

        void Scan(string path)
        {
            ct.ThrowIfCancellationRequested();
            if (Directory.Exists(path))
            {
                foreach (var f in Directory.GetFiles(path, "*", SearchOption.AllDirectories).Order()) Scan(f);
                return;
            }
            bool zip;
            using (var s = File.OpenRead(path)) { var m = new byte[4]; zip = s.Read(m) == 4 && m[0] == 'P' && m[1] == 'K'; }
            if (!zip) { One(path, Path.GetFileName(path)); return; }
            using var z = ZipFile.OpenRead(path);
            foreach (var entry in z.Entries.Where(e => e.Length > 0x1000))
            {
                ct.ThrowIfCancellationRequested();
                var tmp = Path.Combine(Path.GetTempPath(), "eamma-dlc-" + Guid.NewGuid().ToString("N"));
                try
                {
                    entry.ExtractToFile(tmp);
                    One(tmp, Path.GetFileName(entry.FullName));
                }
                finally { try { File.Delete(tmp); } catch { } }
            }
        }

        Scan(source);
        log.Insert(0, ok == 0 ? $"No {Branding.GameName} DLC packages found there." : $"{ok} DLC package(s) installed.");
        return log;
    }

    /// <summary>XCONTENT_AGGREGATE_DATA (0x148 bytes) plus an all-ones license mask: every pack counts as purchased.</summary>
    static byte[] HeaderBytes(string title, string fileName)
    {
        var b = new byte[0x148 + 4];
        void BE32(int o, uint v) { b[o] = (byte)(v >> 24); b[o + 1] = (byte)(v >> 16); b[o + 2] = (byte)(v >> 8); b[o + 3] = (byte)v; }
        BE32(0, 1); BE32(4, 2);
        var nm = Encoding.BigEndianUnicode.GetBytes(title);
        Array.Copy(nm, 0, b, 8, Math.Min(nm.Length, 254));
        var fn = Encoding.ASCII.GetBytes(fileName);
        Array.Copy(fn, 0, b, 0x108, Math.Min(fn.Length, 42));
        BE32(0x140, TitleId);
        b[0x148] = b[0x149] = b[0x14A] = b[0x14B] = 0xFF;
        return b;
    }
}
